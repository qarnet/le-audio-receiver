# Central-Only Test Workflow Cleanup

Status: required correction

## Non-negotiable workflow

No phone, handset, smartphone, or human-operated stream source belongs in this
repository's current test plan. Agents autonomously use the nRF5340DK
`hci_uart` central attached to Linux as `hci0`, then run
`scripts/bap_central.py` to connect to the Xiao and stream LC3 audio. User does
not connect a phone, create a stream, pair a phone, or operate a central.

The only allowed user input is a true physical observation that an agent cannot
make, such as whether sound is audible from connected speakers/headphones after
the agent has completed its test run.

## Goal

Remove phone-directed and stale Realtek/BT540-primary test instructions. Make
all active documentation and agent guidance describe the autonomous
nRF5340DK-`hci_uart` central workflow.

## Scope

Edit documentation/config comments only. Do not alter application source,
partial uncommitted rate-conversion implementation, overlays, Kconfig behavior,
or build scripts.

Search and correct tracked project text including:

- `README.md`
- `AGENTS.md`
- `STATUS.md`
- `SESSION_USB_TABLE.md`
- `docs/**/*.md`
- `.agents/skills/**/*.md`
- `prj.conf` comments

## Required changes

1. Replace every instruction or recommendation to use/test/pair/connect a
   phone with nRF5340DK `hci_uart` central + autonomous
   `scripts/bap_central.py` wording, or remove it if historical-only.
2. Remove stale “hci0 Realtek/BT540 primary” and hci1 fallback workflow.
   Current active central is nRF5340DK `hci_uart` on Linux `hci0` through
   `/dev/ttyACM2` at 1,000,000 H4 flow-control per `STATUS.md`.
3. Update generic pairing text to say “central” rather than “phone.” Preserve
   technical Just Works/MITM rationale without handset instructions.
4. Remove/replace phone-specific codec/pairing examples in stale handoffs and
   skills. Historical documents may retain non-phone evidence only if they no
   longer prescribe obsolete workflows.
5. Ensure no `phone`, `phones`, `smartphone`, `BT540`, or `Realtek` remains in
   tracked project docs/config comments, except an explicit historical-removal
   statement is not needed either. Prefer zero matches.
6. Add a concise “Central-only test rule” to `AGENTS.md` and active plan/status
   docs: agents run the stream autonomously; ask user only for physical audible
   observation after technical test completion.

## Verification

```bash
git grep -inE 'phone|smartphone|BT540|Realtek' -- \
  ':!docs/development/central-only-cleanup-handoff.md' \
  ':!.agents/skills/**/SKILL.md' || true
git diff --check
```

Then include the skill files in an equivalent grep and require no matches.

## Constraints

- Preserve all existing source modifications exactly, including uncommitted
  `audio_rate_convert.*`, `audio_i2s.c`, Kconfig/CMake/board-conf changes, and
  existing receiver diagnostics. Do not stage any source/config file.
- Stage and commit only documentation/comment cleanup plus this handoff.
- Do not run hardware, flash, stream, or ask user to operate a central.
- Do not push, amend, merge, or open a PR.

## Executor recap

Return exact files edited, grep verification, commit hash/message, and confirm
all future active instructions use nRF5340DK `hci_uart` + autonomous script.
