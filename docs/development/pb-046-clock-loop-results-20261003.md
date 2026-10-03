# PB-046: independent timed production sink loop

## Implemented software boundary

`tests/unit/clock_loop/` adds independently clocked finite-FIFO output through real
production sink/drift/ASRC/offload/processor code. Real controller output affects
actual generated word counts; the independent word clock consumes those words,
releases descriptors and changes the slab-free input to the next controller
update. Board tuning is imported, not replaced by legacy drift-unit defaults.

Every submitted and transferred word is compared against independently expressed
global-coordinate expectations. Transfer reads live RAM; an owned-buffer mutation
negative fails even while enqueue capture remains correct. Full frame conservation
includes cancelled stop tail. Fifteen-descriptor capacity, fractional word-clock
credit, timed full-queue wakeup and atomic clock steps are modeled explicitly.

Refined envelope and derivation:
`docs/development/pb-046-clock-model-refinement-20261003.md`. Suite/public-boundary
and ledger details: `tests/unit/clock_loop/README.md`. No codec/LC3plus, physical
clock/IPC/FLPR, RF, analog, presentation or release acceptance is inferred.
Production source and board settings remain unchanged. Frozen baseline, HIL/PCM
limits and canonical BSim 17/26 are preserved.

## Verification and review repairs

The complete focused suite passed **6 PASS / 0 FAIL / 0 SKIP**, terminal
`PROJECT EXECUTION SUCCESSFUL`, after about 209 wall-clock seconds:
`/tmp/opencode/pb046-clock-loop-run-r4.log`, SHA-256
`b0bdc44ef29ba72406778d1ee54f111305823165dee76bdbada8dc6a25c769e9`.
Build log SHA-256
`9fddfe20fc6b497de3388be84a2936b67572b14e61746cb58e09fa4b863db78f`.
No compiler/Kconfig or runtime warning was emitted; scoped native unsupported-SoC
notice and test-only entropy banner remain raw. Native fortify-at-O0 handling
follows existing gate policy; no production warning waiver was introduced.

Positive plants pass 1000-second horizons and declared settled trajectory/rate
criteria. Matched disabled plants fail intended repeat and deadline criteria.
Isolated disturbance cases retain full event/control history. Both injected
remote faults show explicit successful resumption after validated fallback;
permanently failed submissions produce recovery rejection. Stop, idle, overlapped
backpressure and no-helper-reset reopen are verified, with authored reopened data
reaching timed consumption rather than merely remaining queued.

Read-only review identified and drove repairs of unmatched causal controls,
enqueue-only waveform observation, success counters insufficient to prove recovery,
timer-reset masking, bundled stimuli and non-atomic step timestamps. Follow-up
review caught late-success deadline ordering, unchecked final-stop consumption
errors and unbounded synchronous stop supervision. All were repaired and tested;
final review found no remaining substantive issue within declared fixture scope.

Earlier passing run r1 was incomplete acceptance because review exposed these
gaps. Run r2 failed the new overlap test due to stale fixture observation semaphore
from the previous case, not production stop behavior. Fixture preparation now
resets its observation semaphores; targeted lifecycle run passed, then complete
r4 passed. All earlier logs remain retained; none is relabeled full acceptance.

The deadline helper boundary regression is a pure model decision test, not a
delayed-success peer execution. Transport timeout injection is immediate, not
physical latency proof. The fixture is single-CPU; SMP behavior is not claimed.
Exact declared clocks/horizons, model code, input recipe, tuning and complete
source-event ledger define what the result proves, not a full-clamp operating
guarantee. 32-bit cycle-based ztest summary time can wrap over the aggregate
suite; 64-bit model timestamps and per-case horizons are authoritative.

Clean-candidate repository gates and hosted PR checks are still pending at this
checkpoint. PB-046 remains In Progress until they pass. External evidence paths
were available in this session and must be checked before later reuse.
