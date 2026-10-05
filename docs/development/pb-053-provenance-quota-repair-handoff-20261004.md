# PB-053: final provenance and quota cleanup review repairs

## Scope

Repair two independent review defects: source-to-image binding and cleanup after
event quota exhaustion. Files: host preparation/staging code and guest tests;
guest limits/public cleanup and native limits tests; PB-053 notes. No VM/protocol
changes, vendor/SDK/host Bluetooth, PB-051, downloads or unrelated edits. Full
accounted lane9/9 remains evidence, not final acceptance until repairs verified.

## Source-to-image binding

1. At prepare before any copies, freeze source digest map for all nine logical
   source keys (host,guest,public,results,monitor,acquire,limits,process,emulator).
   Emulator expected fixed existing digest. Retain source paths and caps.
2. Stage every Python source and emulator through existing bounded snapshot
   helper, hashing bytes actually copied against frozen digest. Manifest source
   hashes come from frozen/copied bytes, NEVER late rehash of uncopied repo files.
   Emulator needs executable mode0755 after snapshot; Python sources imported or
   run via interpreter can remain0444. Host source itself not in guest image;
   retain its verified snapshot in preparation evidence, not a guest program.
3. Before archive seal/manifest emission rehash each live source and require same
   frozen digest. Drift causes explicit failed preparation, no success manifest.
   Staged source hashes are rechecked before archive generation. No rewriting
   original source or previous prepared images. Add boundary fixture changing
   source after staging and proving rejection/truthful earlier snapshot binding.

## Cleanup after quota failure

4. EventLedger adds `safe_append(value)` only for cleanup. It catches quota/JSON
   failure, records sticky error property and returnsFalse, never evicts/truncates
   accepted data. Normal append keeps raising on quota. Sticky error must always
   make public final result false with cleanup_errors carrying exact reason.
5. In public finally cleanup, every evidence append uses safe_append so closing
   source cannot abort before sink/peer/registration/bus/loop cleanup. Resource
   actions independently guarded; GLib loop stop/join still outermost finally.
   Error append failures must not skip other resource actions. Final outcome
   includes sticky ledger error after loop quiescence. All normal successful
   events and strict schema/order/content validators unchanged.
6. Native test exhausts tiny EventLedger then closes TWO real socketpair leases,
   using actual cleanup helper if factored: both peers observe EOF and failed
   result preserves quota error. No private-field/helper-count acceptance. Test
   caller-visible immutable retained event, quota rejection and continued close.

## Verification

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_guest_limits -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_results -p 'test_*.py' -v
git diff --check
```

No VM/commit until recap reviewed. Preserve all previous evidence. Missing detail,
unexplained warning/failure or two failed attempts: stop exact question/logs, no
weakened limits, source pin substitution or implicit accepted truncation.
