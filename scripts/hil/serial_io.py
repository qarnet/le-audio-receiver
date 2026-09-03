"""Serial console ownership for the system HIL runner (RH2).

``SerialConsole`` owns one pyserial descriptor and one reader thread per
role. Binding-selected ``dtr`` and ``rts`` states are set before the port is
assigned and the backend opened, so no post-open line transition occurs.
Raw bytes are appended to one role-specific flat evidence file before any
parsing; complete lines are decoded as strict UTF-8 and published to a
condition-protected queue. Undecodable bytes fail the row but remain retained
in the raw evidence file. Every encoded TX write is appended and flushed to a
separate role-specific evidence file before the backend write; backend write
failures retain the attempted bytes and fail the console operation.

Every wait polls cancellation no slower than 100 ms.  ``close()`` waits for
the current gated read for at most ``READ_TIMEOUT``, closes the descriptor
without racing that read, joins the reader thread with a bounded timeout,
and reports failure when the thread still lives.
"""

import os
import re
import threading
import time

import serial

#: Read timeout used for every console open (at most 100 ms so waits can
#: poll cancellation quickly).
READ_TIMEOUT = 0.1

#: Reader join budget (bounded; a live thread is reported, never hung).
CLOSE_JOIN_TIMEOUT = 5.0

#: Line queue depth cap; a full queue is a runtime error, never silent
#: loss of records.
LINE_QUEUE_CAP = 4096


# TX evidence keeps same directory and RX evidence stem, with ``-tx`` added
# before suffix. For example, ``receiver-console.bin`` becomes
# ``receiver-console-tx.bin``.
def tx_evidence_path(evidence_path):
    """Return separate TX evidence path for one RX evidence path."""
    evidence_path = os.fspath(evidence_path)
    stem, suffix = os.path.splitext(evidence_path)
    return stem + "-tx" + suffix


# Zephyr's UART shell emits CSI color and cursor-control sequences when
# CONFIG_SHELL_VT100_COMMANDS=y.  Retain those bytes unchanged in evidence,
# but remove complete CSI sequences from host-side transcript framing and
# parsing.  The real receiver prompt is emitted as
# ``ESC[1;32muart:~$ ESC[m`` without a trailing newline.
VT100_CSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def strip_vt100(text):
    """Return ``text`` without complete ANSI/VT100 CSI sequences.

    This is deliberately presentation-only.  Raw serial evidence remains
    byte-for-byte untouched, and an incomplete sequence remains visible until
    later bytes complete it.
    """
    return VT100_CSI_RE.sub("", text)


class SerialConsoleError(Exception):
    """Raised for console open/read/decode/close failures."""


class SerialConsoleCancelled(SerialConsoleError):
    """Raised when a caller cancels a bounded console operation."""


class SerialConsole:
    """One role's serial console: open, reader thread, line queue, raw
    evidence retention, and bounded close."""

    def __init__(
        self,
        role,
        path,
        baud,
        evidence_path,
        serial_class=serial.Serial,
        sleep=time.sleep,
        clock=time.monotonic,
        dtr=False,
        rts=False,
    ):
        if not isinstance(dtr, bool) or not isinstance(rts, bool):
            raise SerialConsoleError("console modem-line states must be booleans")
        self.role = role
        self.path = path
        self.baud = baud
        self.dtr = dtr
        self.rts = rts
        self.evidence_path = evidence_path
        self.tx_evidence_path = tx_evidence_path(evidence_path)
        self._serial_class = serial_class
        self._sleep = sleep
        self._clock = clock
        self._ser = None
        self._stop = threading.Event()
        self._reader_ready = threading.Event()
        self._reader_done = threading.Event()
        self._lines = []
        self._cond = threading.Condition()
        self._decode_error = None  # first strict-UTF-8 decode failure
        self._read_error = None  # first read-loop failure
        self._closed = False
        self._evidence_fh = None
        self._tx_evidence_fh = None
        self._reader_thread = None
        self._line_seq = 0
        self._partial = bytearray()
        # Serial reads and fresh-boot marks share this gate. A mark waits at
        # most one READ_TIMEOUT for an in-flight read, then purges the OS
        # input buffer and clears host parsing atomically relative to the
        # reader. No pre-mark byte can satisfy a post-flash boot gate.
        self._pump_gate = threading.Lock()
        # Absolute offset in raw evidence. ``mark_rx()`` snapshots this
        # counter immediately before a target reset so host parsing can prove
        # boot markers came from the freshly flashed boot, while the flat raw
        # evidence still retains every preceding byte.
        self._bytes_received = 0

    # ── open / reader lifecycle ─────────────────────────────────────

    def open(self):
        """Open the descriptor deasserted and start the reader thread.

        Binding-selected DTR and RTS states are set before the port is
        assigned and before the backend opens; they are never touched
        afterwards. Both evidence files are opened in append-binary before any
        byte can arrive or be written.
        """
        if self._ser is not None:
            raise SerialConsoleError("console %s already open" % self.role)
        ser = None
        try:
            self._evidence_fh = open(self.evidence_path, "ab")
            self._tx_evidence_fh = open(self.tx_evidence_path, "ab")
            ser = self._serial_class()
            # Binding-selected states exist before the backend open: baud/8N1,
            # no flow control, bounded timeout, and no post-open line change.
            ser.baudrate = self.baud
            ser.bytesize = serial.EIGHTBITS
            ser.parity = serial.PARITY_NONE
            ser.stopbits = serial.STOPBITS_ONE
            ser.timeout = READ_TIMEOUT
            ser.xonxoff = False
            ser.rtscts = False
            ser.dsrdtr = False
            ser.dtr = self.dtr
            ser.rts = self.rts
            # Linux pyserial supports TIOCEXCL through ``exclusive``. This
            # fixture has no safe fallback: if the backend exposes the
            # property but cannot honor exclusive ownership, fail before the
            # descriptor opens rather than silently sharing a console.
            if hasattr(ser, "exclusive"):
                try:
                    ser.exclusive = True
                except Exception as exc:  # noqa: BLE001 - backend-specific errno
                    raise SerialConsoleError(
                        "cannot request exclusive %s console: %s" % (self.role, exc)
                    ) from exc
            ser.port = self.path
            ser.open()
        except Exception as exc:  # noqa: BLE001 - open failure is fatal
            if ser is not None:
                try:
                    close = getattr(ser, "close", None)
                    if callable(close):
                        close()
                except Exception:  # noqa: BLE001 - cleanup is best effort
                    pass
            self._close_evidence_handles()
            raise SerialConsoleError(
                "cannot open %s console %s: %s" % (self.role, self.path, exc)
            ) from exc
        self._ser = ser
        try:
            self._reader_thread = threading.Thread(
                target=self._reader_loop,
                name="hil-console-%s" % self.role,
                daemon=True,
            )
            self._reader_thread.start()
        except Exception as exc:  # noqa: BLE001 - thread startup is fatal
            self._stop.set()
            self._ser = None
            try:
                close = getattr(ser, "close", None)
                if callable(close):
                    close()
            except Exception:  # noqa: BLE001 - cleanup is best effort
                pass
            self._close_evidence_handles()
            raise SerialConsoleError(
                "cannot start %s console reader: %s" % (self.role, exc)
            ) from exc

    def _reader_loop(self):
        self._reader_ready.set()
        try:
            while not self._stop.is_set():
                chunk = b""
                try:
                    with self._pump_gate:
                        # close() owns the same gate before it clears the
                        # descriptor. A reader that observed the loop
                        # condition before shutdown can therefore reach this
                        # point after close() has already stopped it.
                        if self._stop.is_set():
                            break
                        ser = self._ser
                        if ser is None:
                            break
                        try:
                            chunk = ser.read(512)
                        except Exception as exc:  # noqa: BLE001 - backend read
                            # A legacy backend can report an exception after
                            # its descriptor was closed. That is intentional
                            # shutdown, not a live-console read failure. Keep
                            # genuine errors visible while the descriptor is
                            # still owned by an active console.
                            if not (self._stop.is_set() and self._ser is None):
                                self._read_error = exc
                            break
                        if chunk:
                            with self._cond:
                                # Raw evidence append, its absolute cursor,
                                # and parser publication form one observable
                                # receive transaction. rx_offset() cannot
                                # observe a file-appended chunk before this
                                # counter includes it.
                                self._append_evidence(chunk)
                                self._bytes_received += len(chunk)
                                self._partial.extend(chunk)
                                self._publish_lines_locked()
                                self._cond.notify_all()
                except Exception as exc:  # noqa: BLE001 - record, stop loop
                    # _append_evidence() records its own failure before
                    # raising. Preserve that first error rather than
                    # replacing it with the surrounding exception.
                    if self._read_error is None:
                        self._read_error = exc
                    break
                if not chunk:
                    continue
        finally:
            self._reader_done.set()

    def _append_evidence(self, chunk):
        try:
            written = self._evidence_fh.write(chunk)
            if written != len(chunk):
                raise OSError(
                    "short raw evidence write: wrote %d of %d bytes"
                    % (written, len(chunk))
                )
            self._evidence_fh.flush()
        except OSError as exc:
            self._read_error = exc
            raise

    def _append_tx_evidence(self, data):
        """Retain one encoded TX attempt before sending it to the backend."""
        try:
            written = self._tx_evidence_fh.write(data)
            if written != len(data):
                raise OSError(
                    "short TX evidence write: wrote %d of %d bytes"
                    % (written, len(data))
                )
            self._tx_evidence_fh.flush()
        except Exception as exc:  # noqa: BLE001 - evidence failure is fatal
            raise SerialConsoleError(
                "cannot retain %s TX evidence: %s" % (self.role, exc)
            ) from exc

    def _publish_lines_locked(self):
        """Split complete lines from shared partial buffer.

        Caller holds ``self._cond``. An unterminated Zephyr shell prompt stays
        observable by prompt-bounded command collection.
        """
        while True:
            idx = self._partial.find(b"\n")
            if idx < 0:
                break
            raw = bytes(self._partial[:idx]).rstrip(b"\r")
            del self._partial[: idx + 1]
            try:
                line = raw.decode("utf-8")
            except UnicodeDecodeError as exc:
                if self._decode_error is None:
                    self._decode_error = exc
                continue  # raw bytes already retained
            if len(self._lines) >= LINE_QUEUE_CAP:
                self._decode_error = self._decode_error or SerialConsoleError(
                    "console line queue overflow"
                )
                continue
            self._lines.append((self._line_seq, line))
            self._line_seq += 1

    def reader_ready(self):
        """True once the reader thread is pumping bytes."""
        return self._reader_ready.is_set()

    def wait_reader_ready(self, timeout):
        deadline = self._clock() + timeout
        while not self._reader_ready.is_set():
            if self._read_error is not None or self._stop.is_set():
                return False
            remaining = deadline - self._clock()
            if remaining <= 0:
                return False
            self._sleep(min(0.05, remaining))
        return True

    def mark_rx(self):
        """Discard host-side pre-mark parse state and return raw byte offset.

        Call immediately before a flash helper resets this target. Raw bytes
        are never removed from evidence. Only completed lines and an
        unterminated pre-mark fragment are excluded from post-reset parser
        decisions, preventing a stale boot banner in a UART FIFO from
        satisfying a required fresh-boot marker.

        Test serial backends may expose ``mark_rx()`` to release scripted
        post-reset bytes. Production ``pyserial.Serial`` exposes no such hook.
        """
        if self._ser is None:
            raise SerialConsoleError("console %s not open" % self.role)
        try:
            with self._pump_gate:
                reset_input = getattr(self._ser, "reset_input_buffer", None)
                if not callable(reset_input):
                    raise SerialConsoleError(
                        "console %s backend cannot purge input" % self.role
                    )
                reset_input()
                with self._cond:
                    self._lines.clear()
                    self._partial.clear()
                    self._cond.notify_all()
                backend_mark = getattr(self._ser, "mark_rx", None)
                if callable(backend_mark):
                    backend_mark()
                # Snapshot after all pre-mark purge work. A test backend's
                # mark hook may synchronously schedule post-reset bytes, but
                # reader gate ownership prevents their consumption until this
                # point. The returned raw offset is therefore exact fresh-RX
                # evidence for the target reset about to follow.
                with self._cond:
                    offset = self._bytes_received
        except Exception as exc:  # noqa: BLE001 - backend-specific errno
            raise SerialConsoleError(
                "cannot mark fresh %s RX edge: %s" % (self.role, exc)
            ) from exc
        return offset

    def rx_offset(self):
        """Return current raw-evidence byte offset without consuming RX data.

        The reader increments this only after it appends a chunk to raw
        evidence. Holding the same condition lock used by line publication
        makes the returned cursor an exact boundary between retained byte
        ranges without changing queued lines, partial framing, or serial
        buffers.
        """
        with self._cond:
            return self._bytes_received

    # ── line consumption ────────────────────────────────────────────

    def next_line(self, timeout):
        """Pop the next complete decoded line, or None on timeout.

        Polls cancellation no slower than 100 ms and returns None on
        timeout, stop, or reader failure.  A reader failure is reported
        through ``read_error()`` and never silently converts to None."""
        deadline = self._clock() + timeout
        while True:
            with self._cond:
                if self._lines:
                    return self._lines.pop(0)[1]
                reader_failed = self._read_error is not None
            if self._stop.is_set() or reader_failed:
                return None
            remaining = deadline - self._clock()
            if remaining <= 0:
                return None
            self._sleep(min(0.05, remaining))

    def drain_lines(self):
        """Return and consume currently decoded lines without waiting.

        Used after the row's terminal source/receiver boundaries, while raw
        capture continues until cleanup. It avoids racing a live evidence file
        during parser reconciliation. Strict decode/read failures remain
        observable through the normal console health checks.
        """
        with self._cond:
            lines = [line for _seq, line in self._lines]
            self._lines.clear()
        return lines

    def wait_line(self, predicate, timeout):
        """Pop lines until one satisfies ``predicate`` (or timeout/None).

        Non-matching lines are discarded: this is the transcript-style
        wait used for receiver boot markers, where every raw byte is
        already retained in the evidence file for the warning scan.
        Returns the matching line or None."""
        deadline = self._clock() + timeout
        while True:
            if self._decode_error is not None:
                raise SerialConsoleError(
                    "console %s invalid UTF-8: %s" % (self.role, self._decode_error)
                )
            line = self.next_line(min(0.2, max(0.0, deadline - self._clock())))
            if self._decode_error is not None:
                raise SerialConsoleError(
                    "console %s invalid UTF-8: %s" % (self.role, self._decode_error)
                )
            if line is None:
                if self._read_error is not None:
                    raise SerialConsoleError(
                        "console %s reader failed: %s" % (self.role, self._read_error)
                    )
                if self._clock() >= deadline or self._stop.is_set():
                    return None
                continue
            if predicate(line):
                return line

    def command_receiver(self, text, prompt, timeout, cancel=None):
        """Send one receiver shell command and return the bounded
        transcript terminated by the return of the prompt after the
        command echo.

        The echo line carries the prompt mid-line. Normal Zephyr UART shell
        output returns its prompt without a line terminator, while some
        terminal/fake backends terminate it; both forms end the transcript.
        Raw bytes are always retained in the evidence file. Returned
        transcript text has VT100 presentation controls removed so real
        colored Zephyr prompts and shell output use the same strict parser as
        plain terminal backends. ``cancel`` is polled between reader slices.
        Raises ``SerialConsoleCancelled`` on cancellation and
        ``SerialConsoleError`` on timeout."""
        if not text:
            raise SerialConsoleError("empty receiver command")
        self.write(text + "\r")
        deadline = self._clock() + timeout
        lines = []
        echo = prompt + text
        echo_seen = False
        while True:
            if cancel is not None and cancel():
                raise SerialConsoleCancelled("receiver command %r cancelled" % text)
            if self._decode_error is not None:
                raise SerialConsoleError(
                    "console %s invalid UTF-8: %s" % (self.role, self._decode_error)
                )
            line = self.next_line(min(0.1, max(0.0, deadline - self._clock())))
            if cancel is not None and cancel():
                raise SerialConsoleCancelled("receiver command %r cancelled" % text)
            if self._decode_error is not None:
                raise SerialConsoleError(
                    "console %s invalid UTF-8: %s" % (self.role, self._decode_error)
                )
            if line is None:
                if self._read_error is not None:
                    raise SerialConsoleError(
                        "console %s reader failed: %s" % (self.role, self._read_error)
                    )
                if echo_seen and self._partial_prompt_ready(prompt):
                    return "\n".join(lines + [strip_vt100(self._partial_text())])
                if self._clock() >= deadline or self._stop.is_set():
                    raise SerialConsoleError(
                        "receiver command %r timed out without prompt return" % text
                    )
                continue
            plain_line = strip_vt100(line)
            lines.append(plain_line)
            if plain_line.endswith(echo):
                echo_seen = True
            # Lines are decoded without CR; the prompt's trailing space
            # must be preserved, so compare without rstrip().
            if echo_seen and plain_line.endswith(prompt):
                return "\n".join(lines)
            if echo_seen and self._partial_prompt_ready(prompt):
                return "\n".join(lines + [strip_vt100(self._partial_text())])

    def _partial_prompt_ready(self, prompt):
        """True only when no complete line remains before returned prompt."""
        with self._cond:
            if self._lines:
                return False
            partial = bytes(self._partial)
        try:
            text = partial.rstrip(b"\r").decode("utf-8")
            return strip_vt100(text).endswith(prompt)
        except UnicodeDecodeError as exc:
            if self._decode_error is None:
                self._decode_error = exc
            return False

    def _partial_text(self):
        """Return current partial receive line without consuming it.

        Zephyr prints its returned shell prompt without a newline. Bytes were
        retained before this observation. Malformed partial UTF-8 marks the
        console invalid and remains in raw evidence.
        """
        with self._cond:
            partial = bytes(self._partial)
        try:
            return partial.rstrip(b"\r").decode("utf-8")
        except UnicodeDecodeError as exc:
            if self._decode_error is None:
                self._decode_error = exc
            return ""

    def write(self, data):
        if self._ser is None:
            raise SerialConsoleError("console %s not open" % self.role)
        encoded = data.encode("utf-8")
        self._append_tx_evidence(encoded)
        try:
            written = self._ser.write(encoded)
        except Exception as exc:  # noqa: BLE001 - backend failure is fatal
            raise SerialConsoleError(
                "console %s serial write failed: %s" % (self.role, exc)
            ) from exc
        if written != len(encoded):
            raise SerialConsoleError(
                "console %s serial write was short: wrote %r of %d bytes"
                % (self.role, written, len(encoded))
            )

    # ── errors ──────────────────────────────────────────────────────

    def decode_error(self):
        """First strict-UTF-8 decode failure, or None."""
        return self._decode_error

    def read_error(self):
        """First read-loop failure, or None."""
        return self._read_error

    # ── close ───────────────────────────────────────────────────────

    def _close_serial_locked(self):
        """Close the backend descriptor while ``_pump_gate`` is held."""
        ser = self._ser
        self._ser = None
        if ser is not None:
            try:
                ser.close()
            except Exception:  # noqa: BLE001 - close is best effort
                pass

    def _close_evidence_handles(self):
        for attribute in ("_evidence_fh", "_tx_evidence_fh"):
            handle = getattr(self, attribute)
            setattr(self, attribute, None)
            if handle is not None:
                try:
                    handle.close()
                except Exception:  # noqa: BLE001 - cleanup is best effort
                    pass

    def close(self):
        """Stop the reader, close its descriptor, and join it boundedly.

        The descriptor close is serialized with ``read()``. Pyserial's
        ``READ_TIMEOUT`` bounds waiting for an in-flight read; the longer
        ``CLOSE_JOIN_TIMEOUT`` remains the final bound for reader shutdown.
        Evidence handles stay open until the reader has stopped so no final
        bytes can race evidence finalization.
        """
        if self._closed:
            return
        self._closed = True

        descriptor_closed = False
        # Do not set stop until this gate is owned. If the active read fails
        # before close reaches the gate, it remains a real reader failure.
        if self._pump_gate.acquire(timeout=READ_TIMEOUT):
            try:
                self._stop.set()
                self._close_serial_locked()
                descriptor_closed = True
            finally:
                self._pump_gate.release()
        else:
            # A compliant pyserial read returns within READ_TIMEOUT. Stop the
            # loop now, then wait for a bounded reader exit before making a
            # second, serialized close attempt. Never close behind the gate.
            self._stop.set()

        thread = self._reader_thread
        reader_still_alive = False
        if thread is not None and thread.is_alive():
            thread.join(CLOSE_JOIN_TIMEOUT)
            if thread.is_alive():
                reader_still_alive = True

        if not descriptor_closed:
            if self._pump_gate.acquire(timeout=READ_TIMEOUT):
                try:
                    self._close_serial_locked()
                    descriptor_closed = True
                finally:
                    self._pump_gate.release()

        # Evidence files are the reader's final output. Do not close them
        # while a live reader could still append a chunk.
        if not reader_still_alive:
            self._close_evidence_handles()

        if reader_still_alive:
            raise SerialConsoleError(
                "console %s reader thread still alive after close" % self.role
            )
        if not descriptor_closed:
            raise SerialConsoleError(
                "console %s descriptor still open after close" % self.role
            )
