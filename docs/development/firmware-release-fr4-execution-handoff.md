# FR4 execution handoff: exact draft assets on both hardware targets

Date: 2026-08-09

User approval: explicit approval received to download draft assets, flash both
receiver boards, and run the full autonomous FR4 hardware matrix.

## Goal

Execute `docs/development/firmware-release-fr4-procedure.md` end to end against
the exact four assets attached to draft release `367572702`. Test exact
downloaded/extracted firmware bytes on nRF5340 E83 and nRF54L15 Xiao. Preserve
complete evidence for Orchestrator review.

Do not write FR4 results or mark FR4 accepted yet. Return raw facts and retained
log inventory. Orchestrator owns evidence review and later acceptance handoff.

## Before hardware

1. Read full project `AGENTS.md`, FR4 procedure, FR3 results, and firmware
   release plan.
2. Inspect git status/log. Current expected HEAD before this handoff commit is
   `91e1ae4`; worktree contains only this untracked handoff.
3. Stage and commit only this file before any download/hardware operation:

   ```text
   docs: record FR4 hardware execution handoff
   ```

4. Do not push, merge, open a PR, or amend.
5. Confirm worktree clean, then run procedure from repo root inside existing
   dev shell.

## Authorized operations

- Authenticated read-only GitHub API queries and downloads for exact draft
  release `367572702` and its four pinned asset IDs.
- Fresh retained run directory under `/tmp/opencode`.
- Read-only probe enumeration/identity checks.
- Normal range flash writes of exact extracted app/companion images to both
  receiver targets, using procedure's no-recovery direct OpenOCD sequences.
- Receiver resets required by normal flash/boot/stream tests.
- UART capture and shell commands (`bt unpair`, `audio status`, FLPR diagnostics).
- Required `btattach`/`btmgmt` setup and raw-HCI connection subprocesses through
  approved repository scripts, including their documented sudo use.
- Autonomous mono, Mode A, Mode B, preserved-bond, 7.5 ms PipeWire, FLPR hang,
  FLPR stall, pairing reset/reconnect/reboot tests.
- Documented non-destructive central cleanup for stale Device1/bond or HCI
  zombie-slot state. Record every remediation.

## Never authorized

- Mass erase, `nrf53_recover`, any recovery path, settings-partition erase,
  APPROTECT change beyond existing normal UICR-unprotected check, or probe-rs.
- Flashing local build outputs, rebuilding firmware, staging extracted files
  into `build/`, changing image bytes, or substituting an asset.
- Editing/deleting/re-uploading/publishing draft release, creating/pushing a
  tag, or any GitHub remote write.
- Firmware/source/test/procedure changes during hardware execution.
- Weakening a criterion, accepting partial evidence, hiding warnings, or
  repeating materially identical failing attempts more than twice.
- Human-operated Bluetooth central. Only repository autonomous central/gates.
- Fresh human input except optional audibility after all autonomous rows; do
  not ask for button/LED interaction.

## Execution order

Follow procedure sections exactly:

1. Create fresh run directory; write its absolute path immediately to recap
   notes.
2. Query and parse exact release metadata before downloads.
3. Download exact four pinned assets by ID; verify exact sizes and SHA-256.
4. Strict top-level checksums, independent `prepare-draft-release.py`,
   provenance/notes byte equality, exact tag-ref HTTP 404.
5. ZIP integrity and extraction into target-specific fresh directories; hash
   exact four extracted image files; never copy into `build/`.
6. Firmware/build identity diff against `3d9a918...`; any executable/build
   input drift blocks flash.
7. Preserve live `nrf-probes` raw identity evidence immediately before each
   target flash. Never assume historical probe mapping.
8. nRF5340 read-only attach/examine. Any lock stops, no recovery. Start UART
   capture before flash/reset. Flash and verify exact app then net images through
   explicit no-recovery procedure. Capture clean boot.
9. Set up autonomous central. Execute nRF5340 fresh mono, fresh Mode A, fresh
   Mode B, immediate preserved-bond Mode B, exact-address 7.5 ms, and APLL
   status. Run target-correct `bt unpair` before each fresh row. Preserve
   target-specific logs and all exits/counters.
10. nRF54L15 live identity, UART-before-flash, direct cpuapp then FLPR load and
    verify, clean boot/FLPR ACTIVE.
11. Execute nRF54L15 fresh mono, fresh Mode A, fresh Mode B, immediate
    preserved-bond Mode B, exact-address 7.5 ms, healthy offload counters,
    180 s Mode A hang gate, live Mode B stall gate plus diagnostics, and exact
    pairing reset/old-bond rejection/fresh pair/preserved reconnect/reboot.
12. Run complete warning/fault scan; classify every warning/error. Expected
    injected FLPR lines must remain inside named windows and reconcile to final
    healthy state.
13. Create `$RUN_DIR/MANIFEST.md` and `$RUN_DIR/SHA256SUMS` covering downloaded
    assets, extracted images, command outputs, UART logs, central logs, gate
    logs, probe logs, flash logs, and metadata. Keep all evidence transient but
    retained through review.

## Stop and escalation rules

Stop before next operation when any procedure criterion fails: asset/metadata
drift, tag unexpectedly exists, identity mismatch, debug lock, flash/verify
failure, missing console, boot warning/error, wrong mode/transport count,
central failure, nonzero decode/underrun/reset, unexplained warning, FLPR gate
failure, stale/incomplete log, or inability to prove exact bytes.

For environmental connection failures, allow at most two materially different
documented attempts and only non-destructive remediation already accepted in
repo evidence. Then stop. Never recover/mass-erase/rebuild/modify code to force
pass.

On blocker preserve run directory and return:

1. exact failed criterion and last safe completed step;
2. commands, exits, relevant raw log excerpts, and hashes;
3. attempts/remediation and why each failed;
4. current target/central state and git status;
5. exact run directory and log inventory;
6. one precise question plus smallest next hypothesis.

## Required recap on completion

Return no acceptance claim. Report:

- handoff commit hash/message and clean git status;
- run directory;
- release metadata, four download hashes/sizes, four extracted image hashes;
- provenance/notes/tag-404 results;
- raw probe identity evidence per target;
- exact flash commands/exits and verify results;
- boot evidence per target;
- every matrix row command, exit, mode, duration/fps/frame count, receiver
  counters, offload/fault counters, and PASS/FAIL;
- warning/error classification;
- all deviations/remediation;
- complete manifest/log hashes;
- blockers and whether every mandatory technical criterion passed;
- ask no audibility question yourself; Orchestrator asks user after review.

Do not commit results, modify docs after the handoff commit, push, publish, or
touch tag/release state.
