#!/usr/bin/env python3
"""LC3 source + PacedWriter lifecycle for bap_central: liblc3
loader/encoder, sine generator, per-mode stream payloads, and the
deterministic writer teardown tail.

Stdlib import ONLY: liblc3 loads lazily at the first LC3Encoder
construction, so stdlib-only module tests, CLI golden tests, and fake
D-Bus suites never require it.  The PacedWriter (bap_central_writer) is
injected as a class so tests can substitute a fake writer/encoder.

Every print and every per-mode payload is byte-compatible with the
bap_central.py flow.
"""

import ctypes
import ctypes.util
import math
import os
import time

import bap_central_writer

from bap_central_endpoint import FRAME_BYTES  # single constant home


class CentralError(Exception):
    """Fatal central-driver error (message already printed by the raiser)."""


PacedWriter = bap_central_writer.PacedWriter

DT_US = 10000  # 10 ms
SR_HZ = 48000
FRAME_SAMPLES = 480  # 48k * 10ms
LC3_PCM_FORMAT_S16 = 0


# ── liblc3 via ctypes (lazy load) ───────────────────────────────────────


def load_liblc3():
    """Load liblc3.so with fallback paths.

    Returns a ctypes.CDLL handle to liblc3.  Called lazily at the first
    LC3Encoder construction (lazy loading keeps stdlib-only tests free
    of the liblc3 dependency).
    """
    # Paths to try, in order.
    NIX_LIBLC3_PATH = (
        "/nix/store/9a1d5s981idlhdhw8dg03bp6arhvrvwn-liblc3-1.1.3/lib/liblc3.so.1"
    )

    # 1. Try system library search first.
    soname = ctypes.util.find_library("lc3")
    if soname:
        try:
            lib = ctypes.CDLL(soname)
            print("[main] liblc3 loaded via find_library:", soname)
            return lib
        except OSError:
            pass

    # 2. Try the known nix-store path.
    if os.path.exists(NIX_LIBLC3_PATH):
        try:
            lib = ctypes.CDLL(NIX_LIBLC3_PATH)
            print("[main] liblc3 loaded via nix-store:", NIX_LIBLC3_PATH)
            return lib
        except OSError:
            pass

    # 3. Scan /nix/store for any liblc3.so.1.
    import glob as _g

    candidates = _g.glob("/nix/store/*-liblc3-*/lib/liblc3.so.1") + _g.glob(
        "/nix/store/*-liblc3*/lib/liblc3.so.1"
    )
    for path in sorted(candidates):
        try:
            lib = ctypes.CDLL(path)
            print("[main] liblc3 loaded via glob scan:", path)
            return lib
        except OSError:
            continue

    raise RuntimeError(
        "liblc3.so.1 not found. Ensure it is installed or update the nix-store path."
    )


_liblc3_cdll = None


def _get_liblc3():
    global _liblc3_cdll
    if _liblc3_cdll is None:
        _liblc3_cdll = load_liblc3()
    return _liblc3_cdll


class LC3Encoder:
    """LC3 audio encoder (ctypes wrapper around liblc3)."""

    def __init__(self, dt_us=DT_US, sr_hz=SR_HZ, frame_bytes=FRAME_BYTES):
        self.dt_us = dt_us
        self.sr_hz = sr_hz
        self.frame_bytes = frame_bytes
        lib = _get_liblc3()

        lib.lc3_encoder_size.restype = ctypes.c_uint
        lib.lc3_encoder_size.argtypes = [ctypes.c_int, ctypes.c_int]

        lib.lc3_setup_encoder.restype = ctypes.c_void_p
        lib.lc3_setup_encoder.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
        ]

        lib.lc3_encode.restype = ctypes.c_int
        lib.lc3_encode.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
        ]

        size = lib.lc3_encoder_size(dt_us, sr_hz)
        self._mem = (ctypes.c_uint8 * size)()
        self._enc = lib.lc3_setup_encoder(dt_us, sr_hz, 0, self._mem)
        if not self._enc:
            raise RuntimeError("lc3_setup_encoder failed")

    def encode(self, pcm_samples):
        """Encode int16 PCM samples to LC3 frame.

        Args:
            pcm_samples: iterable of int16 samples, FRAME_SAMPLES long.

        Returns:
            bytes of length frame_bytes.
        """
        pcm = (ctypes.c_int16 * len(pcm_samples))(*pcm_samples)
        out = (ctypes.c_uint8 * self.frame_bytes)()
        rc = _get_liblc3().lc3_encode(self._enc, 0, pcm, 1, self.frame_bytes, out)
        if rc != 0:
            raise RuntimeError("lc3_encode failed: {}".format(rc))
        return bytes(out)


def gen_sine(freq, sr_hz, nsamples, amplitude=10000):
    """Generate *nsamples* of int16 sine wave at *freq* Hz."""
    samples = [
        int(amplitude * math.sin(2 * math.pi * freq * i / sr_hz))
        for i in range(nsamples)
    ]
    return (ctypes.c_int16 * nsamples)(*samples)


# ── Stream session (encoders + sine + PacedWriter lifecycle) ────────────


class StreamSession:
    """Owns the LC3 encoders, sine PCM, PacedWriter, and the writer
    teardown tail for one stream run.

    ``transports`` is the endpoint transports list (owned by the
    endpoint module); ``start()`` snapshots it for the writer thread so
    teardown can never race a mid-frame read.  The writer keeps feeding
    the receiver through the release window; ``stop_writer()`` is
    idempotent and preserves the exact bounded-join/force tail.
    """

    def __init__(
        self,
        transports,
        stream_mode,
        freq,
        duration_s,
        writer_cls=None,
        encoder_cls=None,
    ):
        self.transports = transports
        self.stream_mode = stream_mode
        self.freq = freq
        self.duration_s = duration_s
        self._writer_cls = writer_cls if writer_cls is not None else PacedWriter
        self._encoder_cls = encoder_cls if encoder_cls is not None else LC3Encoder
        self._enc = None
        self._enc_L = None
        self._enc_R = None
        self._pcm_L = None
        self._pcm_R = None
        self._transports_snapshot = []
        self._writer = None
        self._writer_error = None
        self._writer_stopped = False

    def start(self):
        """Set up encoders per mode (stereo_a sorts transports FL,FR),
        generate the sine PCM, and start the PacedWriter."""
        if self.stream_mode == "stereo_b":
            self._enc_L = self._encoder_cls()
            self._enc_R = self._encoder_cls()
        elif self.stream_mode == "stereo_a":
            # Two mono encoders — one per transport
            self._enc_L = self._encoder_cls()
            self._enc_R = self._encoder_cls()
            # Sort transports: FL (0x01) first, FR (0x02) second
            self.transports.sort(key=lambda t: t["channel_alloc"])
        else:  # mono
            self._enc = self._encoder_cls()

        self._pcm_L = gen_sine(self.freq, SR_HZ, FRAME_SAMPLES, amplitude=10000)
        self._pcm_R = gen_sine(self.freq, SR_HZ, FRAME_SAMPLES, amplitude=10000)

        # Snapshot the transports: the writer thread reads only this list,
        # never the main thread's `endpoint.transports` (which teardown
        # empties), so teardown can never race a mid-frame read.
        self._transports_snapshot = list(self.transports)

        self._writer = self._writer_cls(self.encode_frame, self.duration_s)
        self._writer.start()

    def encode_frame(self):
        """One frame's (fd, payload) pairs — exact per-mode payloads."""
        if self.stream_mode == "stereo_b":
            sdu = self._enc_L.encode(self._pcm_L) + self._enc_R.encode(self._pcm_R)
            return [(self._transports_snapshot[0]["fd"], sdu)]
        if self.stream_mode == "stereo_a":
            frame_L = self._enc_L.encode(self._pcm_L)
            frame_R = self._enc_R.encode(self._pcm_R)
            return [
                (self._transports_snapshot[0]["fd"], frame_L),
                (self._transports_snapshot[1]["fd"], frame_R),
            ]
        sdu = self._enc.encode(self._pcm_L)
        return [(self._transports_snapshot[0]["fd"], sdu)]

    def run(self):
        """Main thread: sleep for the requested duration (the writer paces
        the frames).  Bounded by the duration; a writer error stops it
        early and is reported below.  KeyboardInterrupt is NOT fatal
        (KeyboardInterrupt is not fatal: teardown still runs, exit 0)."""
        try:
            time.sleep(self.duration_s)
        except KeyboardInterrupt:
            print("\n[main] Interrupted during streaming")

        if self._writer.error is not None:
            self._writer_error = self._writer.error
            print("[error] SDU writer failed: {}".format(self._writer.error))

        print(
            "[main] Done: {} frames in {:.2f} s ({:.1f} fps)".format(
                self._writer.frames,
                self.duration_s,
                self._writer.frames / self.duration_s if self.duration_s > 0 else 0,
            )
        )

    def stop_writer(self):
        """Bounded writer join/force tail (idempotent, exact prints).

        Runs only after the transports are released and the fds are
        invalid (or the bounded join timeout expires)."""
        if self._writer is None or self._writer_stopped:
            return
        self._writer_stopped = True

        if not self._writer.join(timeout_s=5.0):
            print("[cleanup] SDU writer still alive after 5 s — forcing stop")
            self._writer.stop()
            self._writer.join(timeout_s=1.0)
        else:
            if self._writer.error is not None and self._writer_error is None:
                self._writer_error = self._writer.error
            if self._writer_error is not None:
                print("[cleanup] SDU writer error: {}".format(self._writer_error))
            elif self._writer.tail_frames > 0:
                print(
                    "[cleanup] Teardown tail: {} frames written after the "
                    "duration (release window)".format(self._writer.tail_frames)
                )
