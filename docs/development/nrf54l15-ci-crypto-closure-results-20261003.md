# PB-034/PB-039: encrypted simulator peer runtime closure

## Failure retained and full runtime audit

Run `37056518050`, head `179e730`, passed source preparation, PHY startup,
unit, coverage and firmware. BSim then failed all attempted scenarios at
security setup. Peer logs retained:

```text
libCryptov1.so: cannot open shared object file: No such file or directory
Could not find the libCrypto library neither in ../lib or in LD_LIBRARY_PATH, is it compiled? => disabling real encryption
client: security timeout
```

Artifact `11249560318` retains these failed BSim logs. This is not acceptance.
The SDK's `-RealEncryption=1` requests real crypto but can fall back when its
library cannot load. Disabling encryption, accepting that warning or skipping
pairing would change acceptance and is not a repair.

A complete installed-source audit found the relevant dynamic loads: default
PHY channel `NtNcable`, default PHY modem `Magic`, and peer `libCryptov1.so`.
The precise build target set is now PHY plus those two models and
`ext_libCryptov1`, with ordinary static dependencies supplied transitively.
No build-everything or host-OpenSSL substitution is used.

## Build and warning contract

`scripts/build-bsim-crypto.sh` verifies the SDK's vendor-modified archive digest
`bc0664e717df9fb05df0bb24dd77276b33fefe40287dd300e95ee87fbf1980f7`,
extracts it into an owned temporary directory and builds the same 32-bit
configuration. It prebuilds the exact `libcrypto.a` input because the upstream
Makefile.library deliberately redirects configure/compiler output to null.
Our commands preserve those diagnostics and use `-Werror`; only generated build
outputs are installed. Tracked SDK sources and the archive are not patched.
Perl is an explicit locked dev-shell dependency rather than an accidental host
tool. GCC command-name handling and `ar r` preserve the legacy build interfaces.

Three archive-specific diagnostic exceptions are source-path/hash scoped in
`scripts/openssl-component-cc.py`, separate from the existing five BabbleSim
component exceptions:

| Source | SHA-256 | Exact exception and reason |
| --- | --- | --- |
| `crypto/bio/bss_log.c` | `db979649ebd92c04c4ec44836eee8320b8fef71ddd9fc118bd5a5e204deae4fe` | `-Wno-stringop-truncation`: copies at most `inl` bytes into allocation `inl+1`, then explicitly terminates `buf[inl]`. |
| `crypto/asn1/x_pkey.c` | `f86e9dbce522471c42009c1536ecc37843c85c3e814cae252290440302b35681` | `-Wno-unused-but-set-variable`: vendor `no-err` build removes reporting that consumes the macro-populated ASN.1 context. |
| `crypto/cmac/cmac.c` | `ea439db212fc198ad1355425d9b09570294ae318bbc5a0207ee213188c6bb215` | `-Wno-stringop-overflow`: generic legacy CMAC analysis includes invalid zero block size; simulated peer APIs use AES ECB/CCM, not CMAC. This is not CMAC validation or a general crypto safety claim. |

Changing any such file requires re-audit; unrelated warnings remain errors.
No package-wide warning flag is disabled. Raw failed strict-build diagnostics
remain retained under `/tmp/opencode/migration-ci-crypto-cold-20261002-r*.log`.
Nix multilib linker `skipping incompatible` search messages remain raw; the
actual installed/probed crypto artifacts are ELF32, not an ignored warning or
permission to accept wrong architecture.

## Actual library boundary and ABI

`scripts/bsim-crypto-probe.c` is compiled as a standalone ELF32 process matching
the nRF54L15BSim peers. It loads the exact installed `.so` with `RTLD_NOW`,
requires all six SDK loader APIs, and checks NIST AES-128/ECB and Bluetooth Core
v4.2 Vol6 PartC section1 legacy/v3 CCM known answers. Correct MICs pass;
corrupted MICs fail; the model's explicit MIC-less v3 operation also passes.
It does not link the crypto implementation into the probe or substitute host
OpenSSL. A successful round trip alone is not the oracle.

The shared runtime checker supervises both crypto and PHY probes with bounded
deadlines and catchable cancellation cleanup. Stage 1 also compares crypto,
probe and both newly compiled peer ELF class/endianness/machine before launching
the scenario matrix. Mixed widths are intentional: PHY/default models are
ELF64, encrypted peers/library/probe ELF32. Ordinary 64-bit Python never loads
the 32-bit crypto library through ctypes.

## Verification and limits

Cold temporary builds now include the real bundled crypto archive, installed
library and ABI-matched probe. Tests reject missing crypto, wrong ELF class,
missing symbols, modified audited OpenSSL source and unrelated compiler
warnings. Existing real loader, timeout, SIGTERM/SIGINT and kill/reap cases stay
active. No physical firmware code, codec/PCM oracle, seeds, model selection,
transport limits or simulation run counts changed.

Focused tests and full local unit gate pass; unchanged local Stage 1 passes
17 scenarios/26 runs with real encryption. Retained command/hash records:

- `/tmp/opencode/migration-ci-crypto-focused-20261003-r3.log`: SHA-256
  `b2de3e6dea02149872dc88dafa6267372f66acf35b9d4a333b291b176aa8e3e2`.
- `/tmp/opencode/migration-ci-crypto-unit-20261003.log`: 77/0/77, SHA-256
  `d2bb6a4495304eee2847e8b9d8ec11ddf72278b7a00cb02ad887734bcf5c2a87`.
- `/tmp/opencode/migration-ci-crypto-stage1-20261003.log`: strict 17/26,
  SHA-256 `eac319468b0bd1afce7ed7c3aef62e1d7f41a089510e656ae53dea63c6f9393a`.

Hosted verification of the full encrypted runtime closure is pending its own
push/run. Prior partial/failing waves remain failures. No new hardware, analog,
active-draft FR4, publication or human-merge acceptance is claimed.
