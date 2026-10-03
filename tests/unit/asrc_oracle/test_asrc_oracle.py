#!/usr/bin/env python3
"""PB-045 independent rational arithmetic through real public C boundaries.

Expected samples use global source coordinates and piecewise-linear segments,
not the DUT's extended-block loop or phase. State transfer is opaque. Exact
quantized arithmetic is not spectral quality, RF, mailbox or physical FLPR proof.
"""

import ctypes
from fractions import Fraction
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
Q = 2**32


def nearest(value):
    """Nearest integer, half ties away from zero (not Python ties-to-even)."""
    magnitude = abs(value)
    result = (2 * magnitude.numerator + magnitude.denominator) // (
        2 * magnitude.denominator
    )
    return -result if value < 0 else result


def increment(input_rate, output_rate, ppm):
    nominal = nearest(Fraction(input_rate * Q, output_rate))
    return Fraction(nominal + nearest(Fraction(nominal * ppm, 1000000)), Q)


class Reference:
    def __init__(self, signal, rates):
        self.signal = signal
        self.rates = rates
        self.position = Fraction(0)
        self.available = 0

    def block(self, frames, ppm):
        self.available += frames
        end = self.available - 1
        step = increment(self.rates[0], self.rates[1], ppm)
        output = []
        while self.position <= end:
            index = self.position.numerator // self.position.denominator
            fraction = self.position - index
            for channel in (0, 1):
                a = self.signal[index][channel]
                value = Fraction(a)
                if fraction:
                    value += fraction * (self.signal[index + 1][channel] - a)
                output.append(nearest(value))
            self.position += step
        return output


class Native:
    def __init__(self, library, rates):
        self.lib = library
        self.handle = self.lib.oracle_stream_new(*rates)
        if not self.handle:
            raise RuntimeError("ASRC init rejected valid rate")

    def close(self):
        self.lib.oracle_stream_free(self.handle)

    def block(self, signal, ppm=0, capacity=2048, backend=0):
        if not isinstance(capacity, int) or capacity < 0:
            raise ValueError("capacity must be a nonnegative integer")
        flat = [sample for frame in signal for sample in frame]
        data = (ctypes.c_int16 * max(1, len(flat)))(*flat)
        sentinel = 23130
        guarded = (ctypes.c_int16 * (capacity * 2 + 16))(
            *([sentinel] * (capacity * 2 + 16))
        )
        output = ctypes.cast(ctypes.byref(guarded, 16), ctypes.POINTER(ctypes.c_int16))
        consumed, produced = ctypes.c_size_t(), ctypes.c_size_t()
        status = self.lib.oracle_stream_process(
            self.handle,
            data,
            len(signal),
            output,
            capacity,
            ppm,
            backend,
            ctypes.byref(consumed),
            ctypes.byref(produced),
        )
        if list(guarded[:8]) != [sentinel] * 8 or list(guarded[-8:]) != [sentinel] * 8:
            raise AssertionError("output capacity guard overwritten")
        if produced.value > capacity or consumed.value > len(signal):
            raise AssertionError("reported frame count exceeds public capacity")
        return (
            status,
            consumed.value,
            produced.value,
            list(output[: produced.value * 2]),
        )


def compile_library(directory, asrc_source=None):
    destination = Path(directory) / "oracle.so"
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(
        compiler
        + [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fPIC",
            "-shared",
            "-O2",
            "-I",
            str(ROOT / "src"),
            str(Path(__file__).with_name("adapter.c")),
            str(asrc_source or ROOT / "src/audio_asrc.c"),
            str(ROOT / "src/flpr_audio_process.c"),
            str(ROOT / "src/flpr_ring.c"),
            "-o",
            str(destination),
        ],
        check=True,
    )
    library = ctypes.CDLL(str(destination))
    library.oracle_stream_new.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    library.oracle_stream_new.restype = ctypes.c_void_p
    library.oracle_stream_free.argtypes = [ctypes.c_void_p]
    library.oracle_stream_reset.argtypes = [ctypes.c_void_p]
    library.oracle_stream_process.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_size_t,
        ctypes.c_int32,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(ctypes.c_size_t),
    ]
    library.oracle_stream_process.restype = ctypes.c_int
    return library


def signal(kind, count):
    rng = random.Random(45045)
    if kind == "silence":
        return [(0, 0)] * count
    if kind == "dc":
        return [(-12345, 23456)] * count
    if kind == "ramp":
        return [
            ((i * 31) % 65536 - 32768, 32767 - (i * 17) % 65536) for i in range(count)
        ]
    if kind == "extrema":
        return [((-32768, 32767) if i % 2 else (32767, -32768)) for i in range(count)]
    if kind == "impulse":
        return [
            (
                (32767 if i % 480 in (0, 479) else 0),
                (-32768 if i % 360 in (0, 359) else 0),
            )
            for i in range(count)
        ]
    return [
        (rng.randint(-32768, 32767), rng.randint(-32768, 32767)) for _ in range(count)
    ]


class ArithmeticOracle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="asrc-oracle-")
        cls.library = compile_library(cls.directory.name)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def verify(self, source, rates, partitions, controls, library=None, backends=None):
        reference = Reference(source, rates)
        native = Native(library or self.library, rates)
        offset = 0
        waveform = []
        try:
            for block, frames in enumerate(partitions):
                expected = reference.block(frames, controls[block])
                status, consumed, produced, actual = native.block(
                    source[offset : offset + frames],
                    controls[block],
                    backend=backends[block] if backends else 0,
                )
                self.assertEqual(status, 0, (block, rates))
                self.assertEqual(consumed, frames, block)
                self.assertEqual(produced * 2, len(expected), (block, "frame count"))
                self.assertEqual(
                    actual, expected, (block, rates, controls[block], "PCM")
                )
                waveform.extend(actual)
                offset += frames
            self.assertEqual(offset, len(source))
            return waveform
        finally:
            native.close()

    def test_reference_known_values(self):
        self.assertEqual(
            [nearest(Fraction(n, 2)) for n in (-3, -1, 1, 3)], [-2, -1, 1, 2]
        )
        self.assertEqual(increment(48000, 48000, -1), Fraction(Q - 4295, Q))
        reference = Reference([(-32768, 0), (32767, 1)], (1, 2))
        self.assertEqual(reference.block(2, 0), [-32768, 0, -1, 1, 32767, 1])

    def test_full_waveforms_and_partition_invariance(self):
        rng = random.Random(450)
        for rates in ((1, 2), (1, 1), (2, 1), (48000, 47619)):
            for kind in ("silence", "dc", "ramp", "extrema", "impulse", "random"):
                for ppm in (-3000, -2000, -1, 0, 1, 500, 2000, 3000):
                    with self.subTest(rates=rates, kind=kind, ppm=ppm):
                        source = signal(kind, 1920)
                        regular = [480] * 4
                        irregular, left = [], len(source)
                        while left:
                            size = min(
                                left, rng.choice((1, 2, 3, 7, 31, 359, 360, 479))
                            )
                            irregular.append(size)
                            left -= size
                        a = self.verify(source, rates, regular, [ppm] * len(regular))
                        b = self.verify(
                            source, rates, irregular, [ppm] * len(irregular)
                        )
                        self.assertEqual(a, b)

    def test_fixed_global_control_schedule(self):
        source = signal("random", 4800)
        controls = (-2000, 1, 2000, -1, 0)
        a = self.verify(source, (48000, 47619), [960] * 5, controls)
        b = self.verify(
            source,
            (48000, 47619),
            [1, 7, 360, 31, 479, 82] * 5,
            [ppm for ppm in controls for _ in range(6)],
        )
        self.assertEqual(a, b)

    def test_capacity_retry_invalid_ppm_and_reset(self):
        source = signal("random", 960)
        reference = Reference(source, (48000, 47619))
        native = Native(self.library, (48000, 47619))
        try:
            for capacity in (0, 1, 5):
                self.assertEqual(native.block(source[:480], 500, capacity)[0], 1)
            self.assertLess(native.block(source[:480], 3001)[0], 0)
            self.assertEqual(native.block([], 0), (0, 0, 0, []))
            for offset in (0, 480):
                if offset:
                    for capacity in (0, 1, 5):
                        self.assertEqual(
                            native.block(source[480:], -500, capacity)[0], 1
                        )
                    self.assertLess(native.block(source[480:], -3001)[0], 0)
                status, consumed, produced, output = native.block(
                    source[offset : offset + 480], 500
                )
                self.assertEqual((status, consumed), (0, 480))
                expected = reference.block(480, 500)
                self.assertEqual(produced * 2, len(expected))
                self.assertEqual(output, expected)
            self.library.oracle_stream_reset(native.handle)
            expected = Reference(source, (48000, 47619)).block(480, -500)
            self.assertEqual(
                native.block(source[:480], -500), (0, 480, len(expected) // 2, expected)
            )
        finally:
            native.close()

    def test_real_processor_handover_and_360_fallback(self):
        source = signal("random", 480 * 4 + 360)
        reference = Reference(source, (48000, 47619))
        native = Native(self.library, (48000, 47619))
        offset = 0
        try:
            for frames, ppm, backend in (
                (480, -2000, 0),
                (480, 2000, 1),
                (360, -1, 1),
                (480, 0, 0),
                (480, 500, 1),
            ):
                part = source[offset : offset + frames]
                if backend and frames == 480:
                    self.assertLess(
                        native.block(part, ppm, capacity=480, backend=1)[0], 0
                    )
                status, consumed, produced, output = native.block(
                    part, ppm, backend=backend
                )
                if frames == 360:
                    self.assertLess(status, 0, "Unsupported offload must reject")
                    self.assertEqual((consumed, produced, output), (0, 0, []))
                    status, consumed, produced, output = native.block(part, ppm)
                self.assertEqual((status, consumed), (0, frames))
                expected = reference.block(frames, ppm)
                self.assertEqual(produced * 2, len(expected))
                self.assertEqual(output, expected)
                offset += frames
        finally:
            native.close()

    def test_compiled_defects_are_rejected(self):
        """Execute altered C, not altered expected output or mocked responses."""
        original = (ROOT / "src/audio_asrc.c").read_text()
        mutations = {
            "negative_ppm_rounding": (
                "(product + ((product < 0) ? -500000 : 500000))",
                "(product + 500000)",
            ),
            "pcm_rounding_sign": (
                "result += (sum >= 0) ? 1 : -1;",
                "result += (sum >= 0) ? -1 : 1;",
            ),
            "ppm_direction": (
                "int64_t product = base_signed * (int64_t)ppm;",
                "int64_t product = -base_signed * (int64_t)ppm;",
            ),
            "boundary_history": (
                "l = interp_s16(prev_l, input[0], frac);",
                "l = interp_s16(0 * prev_l, input[0], frac);",
            ),
            "channel_routing": (
                "output[out * 2] = l;\n\t\toutput[out * 2 + 1] = r;",
                "output[out * 2] = r;\n\t\toutput[out * 2 + 1] = l;",
            ),
            "missing_frames": ("phase += step;", "phase += step + Q32_ONE;"),
            "capacity_overwrite": (
                "output[out * 2 + 1] = r;",
                "output[output_capacity * 2] = r;",
            ),
            "reported_count": (
                "*output_produced = out;\n\treturn 0;",
                "*output_produced = out + output_capacity;\n\treturn 0;",
            ),
        }
        for name, (before, after) in mutations.items():
            with (
                self.subTest(defect=name),
                tempfile.TemporaryDirectory(prefix="asrc-mutant-") as directory,
            ):
                self.assertEqual(
                    original.count(before), 1, "Mutation must match exactly once"
                )
                source = Path(directory) / "audio_asrc.c"
                source.write_text(original.replace(before, after))
                library = compile_library(directory, source)
                with self.assertRaises(AssertionError):
                    self.verify(
                        signal("random", 1920),
                        (48000, 47619),
                        [480] * 4,
                        [-2000] * 4,
                        library=library,
                    )


if __name__ == "__main__":
    unittest.main()
