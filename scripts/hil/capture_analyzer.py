"""Pure NumPy capture oracle for accepted HIL mono/stereo fixtures.

Input is one strict PCM WAV, immutable HIL row, capture capability, and
accepted external qualification. Output is canonical JSON-compatible metrics,
per-metric qualified pass flags, and no hardware acceptance claim. Signal
expectations derive from public source constants, never private output hashes.
"""

import hashlib
import json
import math
import os
import struct
from dataclasses import dataclass

import numpy as np

from hil import capture_signal, qualification, source_contract


ANALYZER_SCHEMA_VERSION = 1
ALGORITHM_VERSION = 1
SAMPLE_RATE = source_contract.PUBLIC_CONSTANTS["HIL_SOURCE_SAMPLE_RATE_HZ"]
SAMPLE_WIDTH_BITS = 16
SAMPLE_WIDTH_BYTES = 2
PREAMBLE_SEGMENT = source_contract.PUBLIC_CONSTANTS[
    "HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES"
]
PREAMBLE_TOTAL = source_contract.PUBLIC_CONSTANTS["HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES"]
ENVELOPE_BLOCK = source_contract.PUBLIC_CONSTANTS["HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES"]
HANN_PROJECTION_SAMPLES = ENVELOPE_BLOCK
FREQUENCY_NEIGHBORHOOD_HZ = 8.0
PREAMBLE_COARSE_STRIDE = source_contract.PUBLIC_CONSTANTS[
    "HIL_SOURCE_TRANSITION_RAMP_SAMPLES"
]
LAG_SEARCH_BLOCKS = 20
LAG_DRIFT_SEARCH_BLOCKS = 10
LOW_ENERGY_EXPECTED_RATIO = 0.2
EXPECTED_SILENCE_ABS_THRESHOLD = 1.0e-12
FULL_SCALE_NORMALIZED = 32767.0 / 32768.0


class CaptureAnalyzerError(ValueError):
    """Raised for malformed WAV input or impossible analysis state."""


@dataclass(frozen=True)
class WavData:
    samples: np.ndarray
    channels: int
    sample_rate: int
    frames: int


def _sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_s16le_wav(path, expected_channels=None):
    """Strict RIFF PCM parser. Rejects repairable-looking malformed input."""
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except OSError as exc:
        raise CaptureAnalyzerError("cannot read WAV: %s" % exc) from None
    if len(raw) < 12 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise CaptureAnalyzerError("WAV must be RIFF/WAVE")
    declared_size = struct.unpack_from("<I", raw, 4)[0]
    if declared_size != len(raw) - 8:
        raise CaptureAnalyzerError("RIFF size mismatch or trailing corruption")
    offset = 12
    fmt = None
    data = None
    while offset < len(raw):
        if offset + 8 > len(raw):
            raise CaptureAnalyzerError("truncated WAV chunk header")
        chunk_id = raw[offset : offset + 4]
        chunk_size = struct.unpack_from("<I", raw, offset + 4)[0]
        start = offset + 8
        end = start + chunk_size
        if end > len(raw):
            raise CaptureAnalyzerError("truncated WAV chunk")
        payload = raw[start:end]
        if chunk_id == b"fmt ":
            if fmt is not None:
                raise CaptureAnalyzerError("duplicate fmt chunk")
            if chunk_size != 16:
                raise CaptureAnalyzerError(
                    "unsupported extensible or malformed fmt chunk"
                )
            fmt = struct.unpack("<HHIIHH", payload)
        elif chunk_id == b"data":
            if data is not None:
                raise CaptureAnalyzerError("duplicate data chunk")
            data = payload
        elif chunk_id in (b"fact", b"LIST", b"JUNK", b"bext", b"cue ", b"smpl"):
            raise CaptureAnalyzerError(
                "unsupported extra WAV chunk %r" % chunk_id.decode("ascii", "replace")
            )
        else:
            raise CaptureAnalyzerError(
                "unknown WAV chunk %r" % chunk_id.decode("ascii", "replace")
            )
        offset = end + (chunk_size & 1)
        if offset > len(raw):
            raise CaptureAnalyzerError("truncated WAV padding")
    if offset != len(raw):
        raise CaptureAnalyzerError("trailing WAV corruption")
    if fmt is None or data is None:
        raise CaptureAnalyzerError("WAV needs one fmt and one data chunk")
    audio_format, channels, rate, byte_rate, block_align, bits = fmt
    if audio_format != 1:
        raise CaptureAnalyzerError("WAV must use uncompressed PCM")
    if channels not in (1, 2):
        raise CaptureAnalyzerError("WAV channel count must be 1 or 2")
    if expected_channels is not None and channels != expected_channels:
        raise CaptureAnalyzerError("WAV channel count drift")
    if rate != SAMPLE_RATE:
        raise CaptureAnalyzerError("WAV sample rate must be 48000 Hz")
    if bits != SAMPLE_WIDTH_BITS or block_align != channels * SAMPLE_WIDTH_BYTES:
        raise CaptureAnalyzerError("WAV must use 16-bit S16_LE PCM")
    if byte_rate != rate * block_align:
        raise CaptureAnalyzerError("WAV byte rate mismatch")
    if len(data) == 0 or len(data) % block_align:
        raise CaptureAnalyzerError("WAV data is empty or frame-truncated")
    values = np.frombuffer(data, dtype="<i2")
    frames = len(values) // channels
    samples = values.reshape(frames, channels).astype(np.float64) / 32768.0
    if not np.isfinite(samples).all():
        raise CaptureAnalyzerError("nonfinite decoded WAV sample")
    return WavData(samples=samples, channels=channels, sample_rate=rate, frames=frames)


def validate_complete_wav(path, expected_channels, minimum_frames=None):
    """Validate complete capture WAV before atomic promotion.

    Capture process ownership rejects short files before final rename. Full
    metric analysis still runs later against frozen qualification limits.
    """
    wav = parse_s16le_wav(path, expected_channels=expected_channels)
    if minimum_frames is not None and wav.frames < minimum_frames:
        raise CaptureAnalyzerError("WAV duration shortfall")
    return wav


def _normalized_correlation(a, b):
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    if a.shape != b.shape or a.size == 0:
        return float("nan")
    an = np.linalg.norm(a)
    bn = np.linalg.norm(b)
    if (
        not math.isfinite(float(an))
        or not math.isfinite(float(bn))
        or an == 0.0
        or bn == 0.0
    ):
        return float("nan")
    return float(np.dot(a, b) / (an * bn))


def _projection_power(samples, frequency):
    """Hann-windowed coherent/noncoherent projection near exact carrier.

    For fixed 120 ms windows, bin spacing is 8.333... Hz. Neighbor definition
    is recorded in result so future analyzer changes cannot silently drift.
    """
    if len(samples) < HANN_PROJECTION_SAMPLES:
        return float("nan")
    window = np.hanning(HANN_PROJECTION_SAMPLES)
    segment = samples[:HANN_PROJECTION_SAMPLES] * window
    frequencies = np.fft.rfftfreq(HANN_PROJECTION_SAMPLES, 1.0 / SAMPLE_RATE)
    spectrum = np.fft.rfft(segment)
    indices = np.flatnonzero(
        np.abs(frequencies - frequency) <= FREQUENCY_NEIGHBORHOOD_HZ
    )
    if len(indices) == 0:
        return float("nan")
    coherent = np.abs(
        np.sum(
            segment
            * np.exp(-2j * np.pi * frequency * np.arange(len(segment)) / SAMPLE_RATE)
        )
    )
    noncoherent = np.sqrt(np.sum(np.abs(spectrum[indices]) ** 2))
    normalization = np.sum(window)
    if normalization == 0.0:
        return float("nan")
    return float(max(coherent, noncoherent) / normalization)


def _window_projection(samples, frequency):
    usable = len(samples) // HANN_PROJECTION_SAMPLES * HANN_PROJECTION_SAMPLES
    if usable == 0:
        return np.array([], dtype=np.float64)
    return np.array(
        [
            _projection_power(
                samples[start : start + HANN_PROJECTION_SAMPLES], frequency
            )
            for start in range(0, usable, HANN_PROJECTION_SAMPLES)
        ],
        dtype=np.float64,
    )


def _energy_template(row, capability):
    # Preamble template uses public carrier/activity segments, not captured PCM.
    expected = capture_signal.output_channels(row, capability)
    return np.sqrt(np.mean(expected[:PREAMBLE_TOTAL] ** 2, axis=1))


def _capture_energy(samples):
    return np.sqrt(np.mean(samples**2, axis=1))


def _find_preamble(samples, row, capability):
    """Find unique six-segment energy/correlation preamble without host timing."""
    observed = _capture_energy(samples)
    template = _energy_template(row, capability)
    if len(observed) < len(template):
        raise CaptureAnalyzerError("WAV shorter than public preamble")
    # 120-sample coarse stride still captures 240 ms public segments and avoids
    # enormous full-rate correlation allocations on 130-second captures.
    stride = PREAMBLE_COARSE_STRIDE
    observed_ds = observed[::stride]
    template_ds = template[::stride]
    if np.linalg.norm(template_ds) == 0.0:
        raise CaptureAnalyzerError("invalid zero preamble template")
    corr = np.correlate(observed_ds, template_ds, mode="valid")
    energies = np.sqrt(
        np.convolve(
            observed_ds**2, np.ones(len(template_ds), dtype=np.float64), mode="valid"
        )
    )
    denom = energies * np.linalg.norm(template_ds)
    scores = np.divide(corr, denom, out=np.full_like(corr, np.nan), where=denom > 0)
    if not np.isfinite(scores).any():
        # Preserve a complete result for silence: qualification can attribute
        # failure to public preamble correlation instead of a parser omission.
        return 0, 0.0, 0.0
    best_index = int(np.nanargmax(scores))
    best = float(scores[best_index])
    masked = scores.copy()
    exclusion = max(1, PREAMBLE_SEGMENT // stride)
    masked[max(0, best_index - exclusion) : best_index + exclusion + 1] = np.nan
    second = float(np.nanmax(masked)) if np.isfinite(masked).any() else -1.0
    # Coarse search chooses segment pattern. Refine around that candidate at
    # full sample resolution so scored metrics do not inherit stride phase.
    coarse = best_index * stride
    lower = max(0, coarse - stride)
    upper = min(len(observed) - len(template), coarse + stride)
    best_full = coarse
    best_full_score = -math.inf
    template_norm = np.linalg.norm(template)
    for candidate in range(lower, upper + 1):
        segment = observed[candidate : candidate + len(template)]
        segment_norm = np.linalg.norm(segment)
        if segment_norm == 0.0:
            continue
        score = float(np.dot(segment, template) / (segment_norm * template_norm))
        if score > best_full_score:
            best_full = candidate
            best_full_score = score
    return best_full, best_full_score, best - second


def _lag_metrics(observed, expected):
    expected_rms = np.sqrt(np.mean(expected**2, axis=1))
    observed_rms = np.sqrt(np.mean(observed**2, axis=1))
    blocks = min(len(expected_rms), len(observed_rms)) // ENVELOPE_BLOCK
    if blocks < 2:
        return float("nan"), float("nan"), float("nan")
    obs = (
        observed_rms[: blocks * ENVELOPE_BLOCK]
        .reshape(blocks, ENVELOPE_BLOCK)
        .mean(axis=1)
    )
    exp = (
        expected_rms[: blocks * ENVELOPE_BLOCK]
        .reshape(blocks, ENVELOPE_BLOCK)
        .mean(axis=1)
    )
    correlation = _normalized_correlation(obs, exp)
    # Restricted lag on block energy avoids disguising a whole-capture offset
    # as a better unrelated periodic correlation.
    max_lag = min(LAG_SEARCH_BLOCKS, blocks - 1)
    candidates = []
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            score = _normalized_correlation(obs[-lag:], exp[:lag])
        elif lag > 0:
            score = _normalized_correlation(obs[:-lag], exp[lag:])
        else:
            score = _normalized_correlation(obs, exp)
        candidates.append((score, lag * ENVELOPE_BLOCK))
    score, lag = max(
        candidates, key=lambda item: item[0] if math.isfinite(item[0]) else -math.inf
    )
    midpoint = blocks // 2
    left_lag = _best_lag(obs[:midpoint], exp[:midpoint])
    right_lag = _best_lag(obs[midpoint:], exp[midpoint:])
    drift = float(right_lag - left_lag)
    return correlation, float(lag), drift


def _best_lag(observed, expected):
    if min(len(observed), len(expected)) < 2:
        return float("nan")
    max_lag = min(LAG_DRIFT_SEARCH_BLOCKS, len(observed) - 1, len(expected) - 1)
    candidates = []
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            score = _normalized_correlation(observed[-lag:], expected[:lag])
        elif lag > 0:
            score = _normalized_correlation(observed[:-lag], expected[lag:])
        else:
            score = _normalized_correlation(observed, expected)
        candidates.append((score, lag * ENVELOPE_BLOCK))
    return max(
        candidates, key=lambda item: item[0] if math.isfinite(item[0]) else -math.inf
    )[1]


def _channel_metrics(
    samples, expected, expected_carriers, unexpected_carriers, preamble_silence
):
    metrics = {}
    expected_power = [_window_projection(samples, hz) for hz in expected_carriers]
    unexpected_power = [_window_projection(samples, hz) for hz in unexpected_carriers]
    expected_joined = (
        np.concatenate(expected_power)
        if expected_power
        else np.array([], dtype=np.float64)
    )
    unexpected_joined = (
        np.concatenate(unexpected_power)
        if unexpected_power
        else np.array([0.0], dtype=np.float64)
    )
    metrics["expected_carrier_power"] = (
        float(np.nanmin(expected_joined)) if len(expected_joined) else float("nan")
    )
    # Carrier content can vary across deterministic 120 ms envelopes. For an
    # unexpected tone, fail on its strongest scored window, not silence windows.
    metrics["unexpected_carrier_power"] = float(np.nanmax(unexpected_joined))
    env_corr, lag, lag_drift = _lag_metrics(samples[:, None], expected[:, None])
    metrics["envelope_correlation"] = env_corr
    metrics["lag_samples"] = abs(lag)
    metrics["lag_drift_samples"] = abs(lag_drift)
    metrics["peak_normalized"] = float(np.max(np.abs(samples)))
    metrics["full_scale_count"] = int(
        np.count_nonzero(np.abs(samples) >= FULL_SCALE_NORMALIZED)
    )
    metrics["clipping_count"] = metrics["full_scale_count"]
    metrics["dc_offset"] = abs(float(np.mean(samples)))
    if len(preamble_silence):
        metrics["noise_floor"] = float(np.sqrt(np.mean(preamble_silence**2)))
    else:
        denom = float(np.dot(expected, expected))
        gain = float(np.dot(samples, expected) / denom) if denom else 0.0
        metrics["noise_floor"] = float(
            np.sqrt(np.mean((samples - expected * gain) ** 2))
        )
    usable = len(samples) // ENVELOPE_BLOCK * ENVELOPE_BLOCK
    if usable == 0:
        moving = np.array([], dtype=np.float64)
    else:
        moving = np.sqrt(
            np.mean(samples[:usable].reshape(-1, ENVELOPE_BLOCK) ** 2, axis=1)
        )
    metrics["moving_rms_min"] = float(np.min(moving)) if len(moving) else float("nan")
    expected_rms = (
        np.sqrt(np.mean(expected[:usable].reshape(-1, ENVELOPE_BLOCK) ** 2, axis=1))
        if usable
        else np.array([], dtype=np.float64)
    )
    low = (
        moving < (expected_rms * LOW_ENERGY_EXPECTED_RATIO)
        if len(moving) and len(expected_rms)
        else np.array([], dtype=bool)
    )
    metrics["low_energy_window_count"] = int(np.count_nonzero(low))
    longest = 0
    current = 0
    for flag in low:
        current = current + 1 if flag else 0
        longest = max(longest, current)
    metrics["longest_low_energy_run"] = longest
    variation = (
        np.abs(np.diff(moving)) if len(moving) > 1 else np.array([], dtype=np.float64)
    )
    metrics["repeat_variation"] = float(np.max(variation)) if len(variation) else 0.0
    metrics["continuity"] = (
        float(np.max(np.abs(np.diff(samples)))) if len(samples) > 1 else float("nan")
    )
    return metrics


def _common_metrics(wav, scored_start, row):
    scored_end = scored_start + capture_signal.scored_samples(row)
    duration = wav.frames / float(wav.sample_rate)
    return {
        "sample_rate_hz": wav.sample_rate,
        "channel_count": wav.channels,
        "sample_width_bits": SAMPLE_WIDTH_BITS,
        "frame_count": wav.frames,
        "duration_seconds": duration,
        "scored_start_sample": scored_start,
        "scored_end_sample": scored_end,
        "scored_duration_seconds": capture_signal.scored_samples(row)
        / float(SAMPLE_RATE),
    }


def analyze_capture(wav_path, row, capability, qualification_record):
    """Analyze one capture against accepted external limits.

    Return canonical dictionary. Unknown or omitted metrics cannot pass because
    qualification evaluation traverses every required selected-capability key.
    """
    if capability not in ("mono", "stereo"):
        raise CaptureAnalyzerError("capture capability must be mono or stereo")
    if qualification_record.capability.value != capability:
        raise CaptureAnalyzerError("qualification capability mismatch")
    channels = 1 if capability == "mono" else 2
    wav = parse_s16le_wav(wav_path, expected_channels=channels)
    required = capture_signal.total_samples(row)
    if wav.frames < required:
        raise CaptureAnalyzerError("WAV duration shortfall")
    preamble_start, preamble_corr, preamble_margin = _find_preamble(
        wav.samples, row, capability
    )
    scored_start = preamble_start + PREAMBLE_TOTAL
    scored_end = scored_start + capture_signal.scored_samples(row)
    if scored_end > wav.frames:
        raise CaptureAnalyzerError("WAV scored interval truncation")
    observed = wav.samples[scored_start:scored_end]
    preamble = wav.samples[preamble_start:scored_start]
    silence_segments = (0, 2, 4)
    expected = capture_signal.scored_output_channels(row, capability)
    metrics = _common_metrics(wav, scored_start, row)
    metrics["preamble_correlation"] = preamble_corr
    metrics["preamble_unique_peak_margin"] = preamble_margin
    if capability == "mono":
        semantic = capture_signal.expected_semantics(row, capability)[0]
        expected_carriers = [
            capture_signal.carrier_frequency(item) for item in semantic
        ]
        unexpected_carriers = (
            [source_contract.PUBLIC_CONSTANTS["HIL_SOURCE_RIGHT_CARRIER_HZ"]]
            if row.mode == "mono"
            else []
        )
        metrics.update(
            _channel_metrics(
                observed[:, 0],
                expected[:, 0],
                expected_carriers,
                unexpected_carriers,
                np.concatenate(
                    [
                        preamble[
                            segment * PREAMBLE_SEGMENT : (segment + 1)
                            * PREAMBLE_SEGMENT,
                            0,
                        ]
                        for segment in silence_segments
                    ]
                ),
            )
        )
    else:
        left_semantic, right_semantic = capture_signal.expected_semantics(
            row, capability
        )
        left_expected = [
            capture_signal.carrier_frequency(item) for item in left_semantic
        ]
        right_expected = [
            capture_signal.carrier_frequency(item) for item in right_semantic
        ]
        left_unexpected = (
            [source_contract.PUBLIC_CONSTANTS["HIL_SOURCE_RIGHT_CARRIER_HZ"]]
            if row.mode != "mono"
            else []
        )
        right_unexpected = (
            [source_contract.PUBLIC_CONSTANTS["HIL_SOURCE_LEFT_CARRIER_HZ"]]
            if row.mode != "mono"
            else []
        )
        left_metrics = _channel_metrics(
            observed[:, 0],
            expected[:, 0],
            left_expected,
            left_unexpected,
            np.concatenate(
                [
                    preamble[
                        segment * PREAMBLE_SEGMENT : (segment + 1) * PREAMBLE_SEGMENT,
                        0,
                    ]
                    for segment in silence_segments
                ]
            ),
        )
        right_metrics = _channel_metrics(
            observed[:, 1],
            expected[:, 1],
            right_expected,
            right_unexpected,
            np.concatenate(
                [
                    preamble[
                        segment * PREAMBLE_SEGMENT : (segment + 1) * PREAMBLE_SEGMENT,
                        1,
                    ]
                    for segment in silence_segments
                ]
            ),
        )
        # Shared mono-named metrics remain aggregate worst-case values.
        for name in qualification.MONO_LIMIT_METRICS:
            if name in metrics:
                continue
            left = float(left_metrics[name])
            right = float(right_metrics[name])
            metrics[name] = (
                max(left, right)
                if name
                in (
                    "unexpected_carrier_power",
                    "peak_normalized",
                    "full_scale_count",
                    "clipping_count",
                    "noise_floor",
                    "dc_offset",
                    "lag_samples",
                    "lag_drift_samples",
                    "low_energy_window_count",
                    "longest_low_energy_run",
                    "repeat_variation",
                )
                else min(left, right)
            )
        for name, value in left_metrics.items():
            metrics["left_" + name] = value
        for name, value in right_metrics.items():
            metrics["right_" + name] = value
        left_997 = float(np.nanmin(_window_projection(observed[:, 0], 997)))
        left_1601 = float(np.nanmin(_window_projection(observed[:, 0], 1601)))
        right_997 = float(np.nanmin(_window_projection(observed[:, 1], 997)))
        right_1601 = float(np.nanmin(_window_projection(observed[:, 1], 1601)))
        observed_channel_correlation = _normalized_correlation(
            observed[:, 0], observed[:, 1]
        )
        expected_channel_correlation = _normalized_correlation(
            expected[:, 0], expected[:, 1]
        )
        if row.mode == "mono":
            metrics["channel_map_margin"] = _normalized_correlation(
                observed[:, 0], observed[:, 1]
            )
            metrics["opposite_channel_leakage"] = 0.0
        else:
            metrics["channel_map_margin"] = min(
                left_997 - left_1601, right_1601 - right_997
            )
            metrics["opposite_channel_leakage"] = max(left_1601, right_997)
        metrics["duplication_correlation"] = abs(
            observed_channel_correlation - expected_channel_correlation
        )
        metrics["observed_channel_correlation"] = observed_channel_correlation
        metrics["expected_channel_correlation"] = expected_channel_correlation
        left_level = math.sqrt(float(np.mean(observed[:, 0] ** 2)))
        right_level = math.sqrt(float(np.mean(observed[:, 1] ** 2)))
        metrics["level_mismatch"] = abs(left_level - right_level)
    names = (
        qualification.MONO_LIMIT_METRICS
        if capability == "mono"
        else qualification.STEREO_LIMIT_METRICS
    )
    for name in names:
        if name not in metrics:
            raise CaptureAnalyzerError("missing metric %s" % name)
        value = metrics[name]
        if not isinstance(value, (int, float)) or not math.isfinite(value):
            # JSON cannot represent NaN/Infinity. Preserve metric presence but
            # force its qualified result to fail through a null value.
            metrics[name] = None
    metric_result, passed = qualification.evaluate_limits(metrics, qualification_record)
    return {
        "schema_version": ANALYZER_SCHEMA_VERSION,
        "algorithm_version": ALGORITHM_VERSION,
        "outcome": "passed" if passed else "failed",
        "wav": {
            "path": wav_path,
            "sha256": _sha256_file(wav_path),
            "sample_rate_hz": wav.sample_rate,
            "channels": wav.channels,
            "sample_width_bits": SAMPLE_WIDTH_BITS,
            "frames": wav.frames,
        },
        "row": {
            "name": row.name,
            "mode": row.mode,
            "profile": row.profile,
            "scored_sdu_count": row.scored_sdu_count,
            "signal_seed": row.signal_seed,
        },
        "capability": capability,
        "qualification": {
            "path": qualification_record.path,
            "sha256": qualification_record.sha256,
            "fixture_id": qualification_record.fixture_id,
            "accepted_by": qualification_record.accepted_by,
            "accepted_at_utc": qualification_record.accepted_at_utc,
        },
        "analysis_constants": {
            "public_source_constants": dict(source_contract.PUBLIC_CONSTANTS),
            "sample_rate_hz": SAMPLE_RATE,
            "preamble_segment_samples": PREAMBLE_SEGMENT,
            "preamble_total_samples": PREAMBLE_TOTAL,
            "envelope_block_samples": ENVELOPE_BLOCK,
            "hann_projection_samples": HANN_PROJECTION_SAMPLES,
            "spectral_window": "hann",
            "frequency_neighborhood_hz": FREQUENCY_NEIGHBORHOOD_HZ,
            "preamble_coarse_stride_samples": PREAMBLE_COARSE_STRIDE,
            "preamble_peak_exclusion_samples": PREAMBLE_SEGMENT,
            "lag_search_blocks": LAG_SEARCH_BLOCKS,
            "lag_drift_search_blocks": LAG_DRIFT_SEARCH_BLOCKS,
            "low_energy_expected_ratio": LOW_ENERGY_EXPECTED_RATIO,
            "expected_silence_abs_threshold": EXPECTED_SILENCE_ABS_THRESHOLD,
            "full_scale_normalized": FULL_SCALE_NORMALIZED,
            "preamble_silence_segments": list(silence_segments),
        },
        "metrics": metric_result,
    }


def canonical_json(result):
    """Serialize analysis evidence with deterministic sorted JSON bytes."""
    return (
        json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False)
        + "\n"
    )
