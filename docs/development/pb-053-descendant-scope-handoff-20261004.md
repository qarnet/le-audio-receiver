# PB-053: nested detached descendant containment

## Goal and scope

Repair outer lane cancellation race: pytest/runner group can be killed before
inner runner kills separately-sessioned QEMU. Direct runner signal controls do
not prove this boundary. Files: new `scripts/bluez_host_descendants.py`, new
`tests/unit/bluez_host_descendants/test_bluez_host_descendants.py`, integration in
lane wrapper and PB-053 notes. Do not change VM/public protocol or guest code.
No host driver/kernel/global policy, hardware, vendor/SDK or PB-051 actions.
No commit/VM until review.

## Verified mechanism

Linux PR_SET_CHILD_SUBREAPER36/PR_GET_CHILD_SUBREAPER37 verified in local kernel
UAPI. Official man7 PR_SET_CHILD_SUBREAPER: orphaned descendants reparent to
nearest ancestor subreaper even across sessions; per-process attribute, not
inherited by fork, preserved by exec. This is ordinary process-local ownership,
not host kernel/module mutation. Existing Python has os.pidfd_open and
signal.pidfd_send_signal. No systemd/root/cgroup prerequisite added.

## Exact design

1. `DescendantScope` context manager, main-thread and dedicated-process only.
   Before entry require no existing direct children and one Python thread. Save
   original subreaper flag through libc prctl, then set flag1. Fail readiness if
   unavailable, never claim cleanup from PID group alone.
2. Caller lane owns all subsequently spawned children, including prerequisite
   children and pytest descendants. Existing run_owned group supervision stays.
   On context exit, normal/error/cancellation, inspect direct children adopted
   after owned parents exit, including detached QEMU sessions. Do not signal
   unrelated host processes or kill by name/guest PID.
3. Enumerate only processes whose actual PPID equals scope owner PID. Before
   signaling, open pidfd and recheck PPID plus /proc start_ticks unchanged;
   pidfd prevents PID-reuse targeting. Reap dead adopted children with waitpid
   WNOHANG; tolerate ChildProcessError only if pidfd proves exit.
4. TERM all live owned adopted children, bounded2s; KILL remaining via pidfd,
   bounded2s. Repeat to catch further grandchildren reparented when their parent
   dies, total cleanup budget8s. Record identities/signals/exit/error; no live
   adopted child may remain on success. If normal body left live descendant,
   cleanup it but mark scope failure (not silent accepted leak). On failed or
   cancelled body, successful descendant cleanup preserves original failure.
5. Restore original subreaper flag and signal handlers after cleanup. Scoped
   SIGINT/SIGTERM handlers outside run_owned record cancellation and enter cleanup;
   cleanup must shield repeated signals, not skip remaining children. Existing
   run_owned signal handler may override temporarily; its failed result still
   triggers scope finally. No inherited unrelated-child reaping (entry check
   prevents existing children, dedicated wrapper has no background threads).
6. API exposes structured `record` with owner PID, saved/restored flag, adopted
   identities, cleanup actions/errors, unexpected_live_descendants and cancelled
   signal. Lane wraps ENTIRE child execution lifetime in scope, retains record in
   suite-record.json and rejects scope failure/cancellation even if JUnit passes.
   Context exits before final suite record sealed. No accepted nested orphan.
7. Scope is outermost host lane only, not inside guest image or normal raw runner.
   No source image hash changes from scope module (wrapper source provenance
   recorded separately). Existing three readiness prerequisites/9-case inventory
   unchanged; scope availability is mandatory execution-owner readiness.

## Real native regression

Separate harness process enters scope, launches intermediary that launches a
new-session child ignoring TERM and then exits or is killed during cancellation.
Signal harness SIGINT/SIGTERM or hit outer timeout, require intermediary and
detached grandchild no longer live, pidfd identities/reap outcomes recorded, failed
execution preserved. Also normal child no descendants succeeds, normal body
leaving detached live descendant gets cleanup plus failed scope, flag restored,
and unrelated sibling process remains live/untouched. No mocks replacing process
or kernel reparent boundary. No actual VM/systemd required in native tests.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_descendants -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_lane -p 'test_*.py' -v
git diff --check
```

Return source/tests before VM. Stop missing prctl/pidfd/ownership detail,
unexplained warning or two failed attempts. Do not weaken nested cleanup, use
arbitrary PID kill, change inventory or add privileged RPC/host mutation.
