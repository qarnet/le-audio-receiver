# RH3-ModeB-control fresh mandatory-row run handoff

Status: approved one-run hardware diagnostic. Direct pre-validation of the
mandatory matrix row `rh3.fresh_mode_b_48_4_1` on the current normal tree.
No diagnostic fragment, no special flags: the normal production image.

## Grounding

Free evidence review of retained runs shows every non-mono shape starves on
hardware while mono 10 ms delivers perfectly:

| Run | Shape | rx_valid | Note |
|---|---|---:|---|
| matrix p1 r1 (20260903) | mono 10 ms | 12644/12644 | PASS, plc=12 |
| matrix p1 r2 (20260903) | Mode A 10 ms | 130 + 0 / 12644 | FAIL limits |
| modea2 (20260904, offload disabled) | Mode A 10 ms | 130 + 0 / 12644 | FAIL limits, FLPR cleared |
| 20260822-06 / -07 (old tree) | Mode B 10 ms | 113/12644 | old images, pre source fixes |
| H40/H42 | Mode B 7.5 ms | 23-24/16859 | traced/untraced |

The 20260822 Mode B numbers predate the source TX-pacing/CIS-retry/depth-3
fixes, so they are not current-tree evidence. A fresh Mode B 10 ms run decides
the isolation split with the current tree:

- PASS under frozen limits: collapse is specific to dual CIS (Mode A);
  next phase is dual-CIS controller/buffer diagnostics. Mode B row is also
  matrix-pre-validated.
- FAIL on limits: collapse is broader (SDU size or general non-mono path);
  mono becomes the only healthy shape and the investigation refocuses on
  what distinguishes 240-byte SDU transport from 120-byte mono.

## Identity contract

Normal image under current HEAD; `images.json` authoritative. FLPR expected
`45ab8d15...`, source `f0e1c5ab...`/`4e4b82f5...`. No absolute CPUAPP pins
(HEAD-dependent); record `git rev-parse HEAD` and clean status.

## Fixed identity

```text
run ID: rh3-modeb-control-20260904 (validate unused first)
row:    rh3.fresh_mode_b_48_4_1
```

## Sequence

1. Preflight: disk gate; HEAD + clean status; run-ID validation; run-dir +
   JUnit ownership; fixture validate; source-image hashes.
2. Normal `fw-build-54l15` (no fragment); prove normal config lines
   (`CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_WARN_EXPERIMENTAL=y`, traces
   unset, `CONFIG_BT_ISO_RX_BUF_COUNT=3`); record hashes.
3. ONE runner invocation (normal flags only), outer timeout 3600000 ms:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modeb-control-20260904 \
  --junit /tmp/opencode/hil-runs/rh3-modeb-control-20260904.junit.xml \
  --row rh3.fresh_mode_b_48_4_1
```

4. Integrity review; per-slot summary; limits verdict; FLPR active
   (submit>=1 success expected at 10 ms) and post-stop snapshots; ISO tail.
5. Binary classification per the grounding table. Classification must cite
   THIS run's runner validation.
6. Result doc `system-hil-rh3-modeb-control-result.md`; resume-state update
   (run count, last flash identity, verdict); commit exactly:

```text
docs(hil): record fresh Mode B control verdict
```

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global `__ASSERT()`).
No production changes, no evidence mutation, no retry, status 0/1/130
immutable. Escalate on unexpected boundary or classification ambiguity.