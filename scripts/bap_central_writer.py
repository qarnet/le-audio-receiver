#!/usr/bin/env python3
"""
Paced SDU writer for bap_central.py — owns the paced fd writes so the
main thread can perform MediaTransport Release / endpoint cleanup while
audio keeps flowing.

Teardown contract (fixes the E83 teardown starvation defect):
  - The writer owns the 10 ms pacing loop and the transport fd writes.
  - It keeps writing until ALL transport fds become invalid (closed by
    the release path) or the main thread sets the stop event — so the
    receiver is fed continuously while BlueZ performs the MediaTransport
    Release transition (the previous design stopped writing at the exact
    duration deadline, letting the I2S pipeline underrun between the last
    SDU and the Release/Disable transition).
  - Exact duration accounting is kept separate from the bounded teardown
    tail: frames written before the duration deadline count as `frames`
    (the fps math), frames written after it count as `tail_frames`.
  - D-Bus stays on the main thread; this module never touches D-Bus.
  - Writer errors are captured and propagated; join is always bounded.

Stdlib only (no liblc3, no dbus) so the unit tests run without the
nix-shell Python.
"""

import os
import threading
import time

FRAME_INTERVAL_S = 0.010  # 10 ms LC3 frame / 100 fps


class PacedWriter:
    """Paced, bounded, single-purpose SDU writer."""

    def __init__(self, encode_fn, duration_s, frame_interval_s=FRAME_INTERVAL_S):
        """
        Args:
            encode_fn: zero-arg callable returning a list of (fd, bytes)
                pairs for the current frame.  Called once per frame on
                the writer thread.  An empty list means no transports
                are left (teardown) and stops the writer.
            duration_s: requested streaming duration in seconds.  Frames
                written before start+duration_s count as `frames`;
                frames after it count as `tail_frames`.
            frame_interval_s: pacing interval per frame.
        """
        self._encode = encode_fn
        self.duration_s = float(duration_s)
        self.frame_interval_s = float(frame_interval_s)
        self.frames = 0  # frames written within the requested duration
        self.tail_frames = 0  # frames written during the teardown tail
        self.error = None  # first writer exception, or None
        self.stopped_by_fd = False  # True when all fds went invalid
        self._stop = threading.Event()
        self._thread = None

    # ── control ──────────────────────────────────────────────────────

    def start(self):
        """Start the writer thread (idempotent)."""
        if self._thread is not None:
            return
        self._thread = threading.Thread(
            target=self._run, name="bap-central-writer", daemon=True
        )
        self._thread.start()

    def stop(self):
        """Ask the writer to stop (it still finishes the current frame
        and drains its pending write)."""
        self._stop.set()

    def join(self, timeout_s=5.0):
        """Join the writer thread with a hard bound.  Returns True if it
        terminated, False if it is still alive (caller must treat a
        still-alive writer as a cleanup failure)."""
        if self._thread is None:
            return True
        self._thread.join(timeout_s)
        return not self._thread.is_alive()

    # ── writer body ──────────────────────────────────────────────────

    def _run(self):
        start = time.monotonic()
        deadline = start + self.duration_s
        next_ts = start

        while not self._stop.is_set():
            try:
                frame = self._encode()
            except Exception as exc:  # propagate to the caller
                self.error = exc
                return

            wrote_any = False
            for fd, data in frame:
                try:
                    os.write(fd, data)
                    wrote_any = True
                except OSError:
                    # Transport fd released/closed (teardown): stop
                    # writing on it, keep the others alive.
                    continue

            if not wrote_any:
                # No transports left / every fd invalid: the release
                # path has completed — stop feeding.
                self.stopped_by_fd = True
                return

            if time.monotonic() < deadline:
                self.frames += 1
            else:
                self.tail_frames += 1

            # Pace: one frame per interval, exact duration accounting.
            next_ts += self.frame_interval_s
            sleep_for = next_ts - time.monotonic()
            if sleep_for > 0:
                if self._stop.wait(sleep_for):
                    return
            else:
                next_ts = time.monotonic()
