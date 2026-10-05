# PB-051 pure-parser target result: UART sealing pending

This result separates actual board action, read-only identity and
orchestrator-reported serial output from a verified raw UART artifact.
Nothing here qualifies physical ASCS, RF, codec, FLPR, DAC, analog or the
PB-041 analyzer harness.

One verified nRF54L15 CPUAPP diagnostic image was loaded through the
explicitly identity-bound probe `158D8E1D`; raw OpenOCD flash log
SHA-256 `74d9b3d72173a07440b6d2099a727ea6555880866a5d022f176f255e738f7525`
contains one `Verifying image:` / `Verified image:` pair and no warning.
Prepared HEX SHA-256
`54c236f21759d6326db427c149825599be7a5507c231655d100ff69c8439177c`.
UART was opened and marked before flash by the orchestrator, not by the
flash owner. Board remains intentionally programmed with the pure-parser
diagnostic, not a streaming role.

The orchestrator reports a complete serial-MCP read: connection
`8c203d1f-c868-46a9-a660-5f767132f17c`, 115200 8N1, flow control
none, profile mode none; from offset 0 through 3759, `bytes_lost=0`,
`truncated=false`, stop drained and connection closed. It reports 12/0/12
tests, `METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`, and
`PROJECT EXECUTION SUCCESSFUL`. This statement is **orchestrator-reported**,
not executor-verified byte acceptance.

The full base64 supplied in conversation could not be transferred exactly
into the external candidate `uart-capture.json`: strict decoding of that
candidate yields **3558**, not 3759 bytes, with visibly corrupted text.
Candidate JSON SHA-256
`8c64231733f0da624ea0c38e31bbf0f7a9362f1a1acca7629f60bda9dd28c1af`.
`/tmp/opencode/pb051-ltv-physical-guard-execution-r1/uart-verdict.json`
explicitly sets `accepted=false`. No `uart.log` was written: repairing a
transcribed base64 string from expected test names or summary would invent
evidence. Owner must provide the original exact MCP raw payload as a file
or exact independently transferred base64 before UART SHA-256, bytewise
test result and physical parser acceptance can be sealed. No UART replay,
reflash, reset or alternate-board attempt is implied.

Independent **post-test read-only** probe/USB check succeeded without board
or UART action: raw DPIDR `0x6ba02477`, AP0/1/2/3
`0x84770001`/`0x84770001`/`0x32880000`/`0x00000000`, FICR PART
`0x00054b15`, VARIANT `0x41414330` (AAC0), plus the current session's
unique VID `2886`, PID `0066`, interface `02` and USB serial
`158D8E1D` on `/dev/ttyACM3`. Evidence:
`/tmp/opencode/pb051-ltv-physical-guard-execution-r1/posttest-identity-proof.json`
SHA-256 `76323afcd36bf28656fd7c29c5583bcbef89ad9cd1bf7d47029c4159e89c9212`;
`posttest-identity.json` SHA-256
`b17c101bc2286b2c0b88104f47cb26951e4b15cd2180e61d23f6d4080144284b`.
These identifiers are fresh-session diagnostic evidence, not a permanent
probe-to-role table.
