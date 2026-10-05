"""Real-file and subprocess contract tests for independent PB-051 accounting."""

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from ascs_results import (  # noqa: E402
    TraceError,
    _events,
    _helper_cp,
    _metadata_echo,
    _request_bytes,
    _response_bytes,
    check_execution_record,
    check_family_logs,
    load_inventory,
)

POLICY_PATH = ROOT / "tests/ascs_bsim/cases.json"
ANCHOR = "addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c"
FAMILY = "control_frame_validation"
RUN = "ab" * 16


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


class ControlTrace:
    """Author complete synthetic control family without copying DUT transcript."""

    def __init__(self, policy):
        self.policy = policy
        self.client = []
        self.receiver = []
        self.tick = 0
        self.ids = {"A": (1, 20), "B": (2, 21)}
        self.tx = 0
        self.phase = 0
        self.procedure = 0

    def emit(self, role, body):
        self.tick += 1000
        line = f"d_{role}: @00:00:{self.tick // 1000000:02}.{self.tick % 1000000:06}  {body}\n"
        (self.client if role == "01" else self.receiver).append(line)

    def c(self, marker, **params):
        self.emit(
            "01",
            marker + " " + " ".join(f"{key}={value}" for key, value in params.items()),
        )

    def read(self, ase, state=0):
        num, handle = self.ids[ase]
        raw = bytes((num, state))
        self.c(
            "ASCS_READ",
            generation=1,
            index=0 if ase == "A" else 1,
            id=num,
            handle=handle,
            state=state,
            raw=raw.hex(),
        )
        return raw

    def render(self):
        # Authored success traffic: two Codec Configure, one ordered QoS,
        # then two distinct Enable procedures, not one invented batch.
        for opcode, ase_ids in (
            (1, (1,)),
            (1, (2,)),
            (2, (1, 2)),
            (3, (1,)),
            (3, (2,)),
        ):
            self.start_procedure("helper", opcode, ase_ids, 0)
            raw = bytes((opcode, len(ase_ids))) + b"".join(
                bytes((ase_id, 0, 0)) for ase_id in ase_ids
            )
            self.c(
                "ASCS_CP",
                transaction=0,
                generation=1,
                procedure=self.procedure,
                owner="helper",
                raw=raw.hex(),
            )
            self.end_procedure("helper", opcode, ase_ids, 0)
        self.phase += 1
        for index in (0, 1):
            self.c(
                "ASCS_TX_REGISTER", generation=1, phase=self.phase, index=index, ret=0
            )
            self.c(
                "ASCS_TX_AUDIT",
                stage="active",
                generation=1,
                phase=self.phase,
                index=index,
                forget_ret=-16,
            )
        self.emit("00", f"ASCS_RENDER phase={self.phase} pushes=27")
        self.c("ASCS_RECOVERY", phase=self.phase, generation=1, rendered=27)
        for index in (0, 1):
            self.c(
                "ASCS_TX",
                phase=self.phase,
                index=index,
                generation=1,
                sends=30,
                expected=30,
            )
            self.c("ASCS_TX", phase=self.phase, index=index, generation=1, unregister=0)
            self.c(
                "ASCS_TX_AUDIT",
                stage="retained",
                generation=1,
                phase=self.phase,
                index=index,
                ret=0,
                sends=30,
                fnv=f"{self.phase * 2 + index:08x}",
            )

    def start_procedure(self, owner, opcode, ase_ids, transaction):
        self.procedure += 1
        self.c(
            "ASCS_PROCEDURE_BEGIN",
            procedure=self.procedure,
            generation=1,
            owner=owner,
            opcode=opcode,
            ids=",".join(map(str, ase_ids)),
            transaction=transaction,
        )

    def end_procedure(self, owner, opcode, ase_ids, transaction):
        self.c(
            "ASCS_PROCEDURE_END",
            procedure=self.procedure,
            generation=1,
            owner=owner,
            opcode=opcode,
            ids=",".join(map(str, ase_ids)),
            transaction=transaction,
        )

    def build(self):
        spec = self.policy["families"][0]
        self.c("ASCS_DISCOVERY", stage="service", generation=1)
        self.c("ASCS_CCC", stage="found", generation=1)
        self.c(
            "ASCS_DISCOVERY",
            generation=1,
            mtu=65,
            cp=22,
            ccc=23,
            ase0="1/20",
            ase1="2/21",
        )
        for step, case in enumerate(spec["cases"], 1):
            self.c(
                "CASE_BEGIN",
                name=case["id"],
                step=step,
                generation=1,
                recovery_phase=self.phase,
            )
            for diag in case["diagnostics"]:
                for _ in range(diag["count"]):
                    self.emit(
                        "00", f"<{diag['severity']}> {diag['module']}: {diag['text']}"
                    )
            # Every case has one rejected action followed by two legal cleanup actions.
            for action in case["actions"]:
                self.tx += 1
                tx = self.tx
                prior = {ase: self.read(ase) for ase in ("A", "B")}
                if "pre_states" in action:
                    for index, ase in enumerate(("A", "B")):
                        self.c(
                            "ASCS_PRESERVE",
                            name=case["id"],
                            phase="before",
                            transaction=tx,
                            generation=1,
                            index=index,
                            raw=prior[ase].hex(),
                        )
                request = _request_bytes(
                    action["request"], self.ids, {}, self.policy["profile"]
                )
                release = (
                    action["request"].startswith("hex:0801")
                    and action["response"]["records"][0]["code"] == 0
                )
                if release and action is case["actions"][1]:
                    self.render()
                cp_ids = tuple(
                    self.ids[rec["ase"]][0] if rec["ase"] != "none" else 0
                    for rec in action["response"]["records"]
                )
                opcode = request[0]
                self.start_procedure("raw", opcode, cp_ids, tx)
                self.c(
                    "ASCS_REQUEST",
                    name="legal-release" if release else case["id"],
                    transaction=tx,
                    generation=1,
                    raw=request.hex(),
                )
                self.c("ASCS_WRITE", transaction=tx, generation=1, ret=0, att_error=0)
                self.c(
                    "ASCS_CP",
                    transaction=tx,
                    generation=1,
                    procedure=self.procedure,
                    owner="raw",
                    raw=_response_bytes(action["response"], self.ids).hex(),
                )
                self.end_procedure("raw", opcode, cp_ids, tx)
                if "pre_states" in action:
                    for index, ase in enumerate(("A", "B")):
                        after = self.read(ase)
                        self.c(
                            "ASCS_PRESERVE",
                            name=case["id"],
                            phase="after",
                            transaction=tx,
                            generation=1,
                            index=index,
                            raw=after.hex(),
                        )
            for ase in ("A", "B"):
                self.read(ase)
            self.c(
                "CASE_END",
                name=case["id"],
                step=step,
                generation=1,
                recovery_phase=self.phase,
                assertions=len(case["actions"]),
                exchanges=len(case["actions"]),
                records=len(case["actions"]),
                preserved=1,
                metadata_observed=1,
                fresh_idle=1,
                rendered=1,
            )
        self.c("ASCS_CP_CLOSE", generation=1, closing=1, notify="NULL")
        self.c("ASCS_CP_CLOSE", generation=1, unsubscribe=0)
        for index in (0, 1):
            self.c(
                "ASCS_TX_AUDIT",
                stage="retire",
                generation=1,
                index=index,
                result_ret=0,
                sends=30,
                fnv=f"{self.phase * 2 + index:08x}",
            )
            self.c("ASCS_TX_AUDIT", stage="forget", generation=1, index=index, ret=0)
            self.c(
                "ASCS_TX_AUDIT",
                stage="forgotten",
                generation=1,
                index=index,
                result_ret=-61,
            )
        self.c("ASCS_CLEANUP", generation=1, retired=1, cp_closed=1)
        self.emit(
            "01", f"INFO: ASCS_CLIENT case={FAMILY} cases=7 assertions=21 phases=7"
        )
        self.emit("00", "INFO: ASCS_RECEIVER phases=7")
        return "".join(self.client).encode(), "".join(self.receiver).encode()


class ResultsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = load_inventory(POLICY_PATH.read_bytes(), ANCHOR)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ascs-result-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.client, self.receiver = ControlTrace(self.policy).build()

    def make_record(self):
        record = dict(
            schema_version=1,
            run_id=RUN,
            family=FAMILY,
            images={},
            logs={},
            participants={},
            scope=dict(
                owner_pid=800,
                saved_flag=0,
                restored_flag=0,
                adopted=[],
                actions=[],
                errors=[],
                unexpected_live_descendants=False,
                cancelled_signal=None,
                ok=True,
            ),
        )
        session = f"ascs_{RUN}_{FAMILY}"
        for idx, role in enumerate(("receiver", "client", "phy")):
            image = bytearray(64 if role == "phy" else 52)
            image[:6] = b"\x7fELF\x02\x01" if role == "phy" else b"\x7fELF\x01\x01"
            image[18:20] = b"\x3e\x00" if role == "phy" else b"\x03\x00"
            img = self.root / f"{role}.elf"
            img.write_bytes(image)
            log = self.root / f"{role}.log"
            raw = {"receiver": self.receiver, "client": self.client, "phy": b""}[role]
            log.write_bytes(raw)
            record["images"][role] = dict(
                path=str(img), bytes=len(image), sha256=digest(image)
            )
            record["logs"][role] = dict(
                path=str(log), bytes=len(raw), sha256=digest(raw)
            )
            argv = [str(img), "-v=2", f"-s={session}"]
            argv += (
                ["-D=2", "-nodump", "-sim_length=250e6"]
                if role == "phy"
                else [
                    f"-d={0 if role == 'receiver' else 1}",
                    f"-testid={FAMILY}",
                    "-RealEncryption=1",
                    f"-rs={23 if role == 'receiver' else 28}",
                ]
            )
            record["participants"][role] = dict(
                schema_version=1,
                argv=argv,
                pid=801 + idx,
                start_time=1.0,
                end_time=2.0,
                returncode=0,
                ok=True,
                timed_out=False,
                cancelled_signal=None,
                log_limit_exceeded=False,
                bytes_logged=len(raw),
                log_sha256=digest(raw),
                cleanup_errors=[],
                error=None,
                descendant_cleanup_required=False,
            )
        return record

    def cli(self, record, *, anchor=ANCHOR, run=RUN):
        destination = self.root / "execution.json"
        destination.write_text(json.dumps(record))
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/check-ascs-results.py"),
                "--inventory",
                str(POLICY_PATH),
                "--expected-inventory-sha256",
                anchor,
                "--family",
                FAMILY,
                "--client-log",
                record["logs"]["client"]["path"],
                "--receiver-log",
                record["logs"]["receiver"]["path"],
                "--execution-record",
                str(destination),
                "--expected-run-id",
                run,
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_complete_synthetic_public_trace_and_cli(self):
        trace = check_family_logs(self.policy, FAMILY, self.client, self.receiver)
        self.assertEqual((trace["raw_exchanges"], trace["render_phases"]), (21, 7))
        result = self.cli(self.make_record())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(json.loads(result.stdout)["accepted"])

    def test_tx_audit_public_negative_controls(self):
        original = self.client
        lines = original.splitlines(keepends=True)
        active = next(line for line in lines if b"stage=active" in line)
        retained = next(line for line in lines if b"stage=retained" in line)
        forget = next(line for line in lines if b"stage=forget " in line)
        gone = next(line for line in lines if b"stage=forgotten" in line)
        retired = next(line for line in lines if b"stage=retire " in line)
        variants = {
            "missing audit": original.replace(active, b"", 1),
            "duplicate audit": original.replace(active, active + active, 1),
            "active forget succeeds": original.replace(
                active, active.replace(b"forget_ret=-16", b"forget_ret=0"), 1
            ),
            "retained count": original.replace(
                retained, retained.replace(b"sends=30", b"sends=29"), 1
            ),
            "retained FNV": original.replace(
                retired, retired.replace(b"fnv=0000000e", b"fnv=ffffffff"), 1
            ),
            "missing explicit forget": original.replace(forget, b"", 1),
            "result still exists": original.replace(
                gone, gone.replace(b"result_ret=-61", b"result_ret=0"), 1
            ),
            "wrong generation": original.replace(
                active, active.replace(b"generation=1", b"generation=2"), 1
            ),
            "wrong timing": original.replace(active, b"", 1) + active,
            "never-used result unexpected": original.replace(
                retired, retired.replace(b"stage=retire", b"stage=unused"), 1
            ),
        }
        for name, raw in variants.items():
            with self.subTest(name=name):
                self.assertNotEqual(raw, original)
                with self.assertRaises(TraceError):
                    check_family_logs(self.policy, FAMILY, raw, self.receiver)
                self.client = raw
                result = self.cli(self.make_record())
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertFalse(json.loads(result.stdout)["accepted"])
        self.client = original

    def test_fail_closed_execution_negative_controls(self):
        original = self.make_record()
        variants = [
            ("digest", lambda r: r["images"]["client"].update(sha256="0" * 64)),
            ("skip", lambda r: r["participants"]["receiver"].update(ok=False)),
            ("timeout", lambda r: r["participants"]["phy"].update(timed_out=True)),
            ("ret", lambda r: r["participants"]["client"].update(returncode=1)),
            ("argv", lambda r: r["participants"]["client"]["argv"].append("-other")),
            (
                "phy missing -nodump",
                lambda r: r["participants"]["phy"].update(
                    argv=[a for a in r["participants"]["phy"]["argv"] if a != "-nodump"]
                ),
            ),
            ("scope", lambda r: r["scope"].update(restored_flag=1)),
            ("log", lambda r: r["logs"]["receiver"].update(sha256="1" * 64)),
        ]
        for name, modify in variants:
            with self.subTest(name=name):
                record = copy.deepcopy(original)
                modify(record)
                result = self.cli(record)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertFalse(json.loads(result.stdout)["accepted"])
        self.assertEqual(self.cli(original, anchor="0" * 64).returncode, 1)
        self.assertEqual(self.cli(original, run="cd" * 16).returncode, 1)

    def test_mixed_abi_required_for_real_cli(self):
        for role, size, klass, machine in (
            ("phy", 64, 1, 3),
            ("receiver", 64, 2, 62),
            ("client", 64, 2, 62),
            ("phy", 52, 2, 62),
        ):
            with self.subTest(role=role, size=size, klass=klass, machine=machine):
                record = self.make_record()
                image = bytearray(size)
                image[:6] = b"\x7fELF" + bytes((klass, 1))
                image[18:20] = machine.to_bytes(2, "little")
                Path(record["images"][role]["path"]).write_bytes(image)
                record["images"][role].update(bytes=size, sha256=digest(image))
                self.assertEqual(self.cli(record).returncode, 1)

    def test_execution_overlap_and_descendant_scope(self):
        original = self.make_record()
        adopted = {"pid": 900, "ppid": 800, "start_ticks": 50}
        normal = copy.deepcopy(original)
        normal["scope"]["adopted"] = [adopted]
        normal["scope"]["actions"] = [{"pid": 900, "start_ticks": 50, "wait_status": 0}]
        self.assertEqual(self.cli(normal).returncode, 0)
        narrow = copy.deepcopy(normal)
        narrow["participants"]["phy"].update(start_time=1.999999, end_time=2.000001)
        self.assertEqual(self.cli(narrow).returncode, 0)
        variants = [
            ("zero time", lambda r: r["participants"]["client"].update(start_time=0)),
            (
                "negative time",
                lambda r: r["participants"]["client"].update(start_time=-2),
            ),
            (
                "disjoint",
                lambda r: r["participants"]["phy"].update(start_time=3, end_time=4),
            ),
            (
                "boundary touch",
                lambda r: r["participants"]["phy"].update(start_time=2, end_time=3),
            ),
            ("scope PID collision", lambda r: r["scope"].update(owner_pid=801)),
            ("wrong parent", lambda r: r["scope"]["adopted"][0].update(ppid=801)),
            (
                "duplicate adopted",
                lambda r: r["scope"]["adopted"].append(adopted.copy()),
            ),
            ("missing reap", lambda r: r["scope"].update(actions=[])),
            (
                "duplicate reap",
                lambda r: r["scope"]["actions"].append(r["scope"]["actions"][0].copy()),
            ),
            (
                "nonzero reap",
                lambda r: r["scope"]["actions"][0].update(wait_status=256),
            ),
            (
                "signalled reap",
                lambda r: r["scope"]["actions"][0].update(wait_status=9),
            ),
            (
                "signal cleanup",
                lambda r: r["scope"]["actions"][0].update(signal=15, wait_status=0),
            ),
            ("unknown child", lambda r: r["scope"]["actions"][0].update(pid=901)),
        ]
        for name, change in variants:
            with self.subTest(name=name):
                record = copy.deepcopy(normal)
                change(record)
                result = self.cli(record)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_real_snapshot_mutation_and_symlink_rejected(self):
        record = self.make_record()
        with open(record["logs"]["client"]["path"], "ab") as stream:
            stream.write(b"extra")
        self.assertEqual(self.cli(record).returncode, 1)
        record = self.make_record()
        symlink = self.root / "indirection.log"
        symlink.symlink_to(record["logs"]["client"]["path"])
        record["logs"]["client"]["path"] = str(symlink)
        self.assertEqual(self.cli(record).returncode, 1)

    def test_snapshot_inode_aliases_and_phy_diagnostics(self):
        record = self.make_record()
        alias = self.root / "same-phy.log"
        alias.hardlink_to(record["logs"]["client"]["path"])
        record["logs"]["phy"].update(
            path=str(alias), bytes=len(self.client), sha256=digest(self.client)
        )
        record["participants"]["phy"].update(
            bytes_logged=len(self.client), log_sha256=digest(self.client)
        )
        self.assertEqual(self.cli(record).returncode, 1)
        record = self.make_record()
        parent_alias = self.root / "parent-link"
        parent_alias.symlink_to(self.root, target_is_directory=True)
        record["images"]["phy"]["path"] = str(parent_alias / "receiver.elf")
        record["participants"]["phy"]["argv"][0] = record["images"]["phy"]["path"]
        self.assertEqual(self.cli(record).returncode, 1)
        record = self.make_record()
        cross = self.root / "elf-as-phy.log"
        cross.hardlink_to(record["images"]["receiver"]["path"])
        binary = cross.read_bytes()
        record["logs"]["phy"].update(
            path=str(cross), bytes=len(binary), sha256=digest(binary)
        )
        record["participants"]["phy"].update(
            bytes_logged=len(binary), log_sha256=digest(binary)
        )
        self.assertEqual(self.cli(record).returncode, 1)
        for marker in (
            b"<wrn> phy: bad",
            b"WARNING: bad",
            b"ERROR: bad",
            b"FATAL ERROR: bad",
            b"FAILED: bad",
        ):
            with self.subTest(marker=marker):
                record = self.make_record()
                phy = self.root / "phy.log"
                phy.write_bytes(marker)
                record["logs"]["phy"].update(bytes=len(marker), sha256=digest(marker))
                record["participants"]["phy"].update(
                    bytes_logged=len(marker), log_sha256=digest(marker)
                )
                self.assertEqual(self.cli(record).returncode, 1)
        record = self.make_record()
        info = b"PHY INFO: normal startup\n"
        (self.root / "phy.log").write_bytes(info)
        record["logs"]["phy"].update(bytes=len(info), sha256=digest(info))
        record["participants"]["phy"].update(
            bytes_logged=len(info), log_sha256=digest(info)
        )
        self.assertEqual(self.cli(record).returncode, 0)

    def test_successful_metadata_requires_public_read_not_marker(self):
        def build(opcode, case, state, marker=True, *, tamper=False, read=True):
            # Raw bytes authored independently: QoS CIG 00/CIS 00, then Enable
            # or Update changes metadata from 03 02 01 00 to 03 02 f0 bb.
            raw = bytes.fromhex("01030000040302f0bb")
            if tamper:
                raw = bytes.fromhex("01030000040302f0aa")
            raw = raw[:1] + bytes((state,)) + raw[2:]
            lines = [
                "ASCS_READ generation=1 id=1 handle=20 state=2 raw=0102000010270000027800051400409c00",
                f"ASCS_REQUEST transaction=1 generation=1 raw={opcode:02x}0101040302f0bb",
                f"ASCS_CP transaction=1 generation=1 raw={opcode:02x}01010000",
            ]
            if read:
                lines.append(
                    f"ASCS_READ generation=1 id=1 handle=20 state={state} raw={raw.hex()}"
                )
            if marker:
                lines.append(
                    f"ASCS_METADATA generation=1 index=0 state={state} cig=0 cis=0 raw={raw.hex()}"
                )
            events = _events(
                "".join(
                    f"d_01: @00:00:00.{index:06}  {line}\n"
                    for index, line in enumerate(lines, 1)
                ).encode(),
                "01",
            )
            req, cp = events[1:3]
            reads = [
                (event, 1, "A", bytes.fromhex(event["fields"]["raw"]))
                for event in events
                if event["marker"] == "ASCS_READ"
            ]
            transactions = [
                (
                    req,
                    cp,
                    {"records": [{"code": 0, "ase": "A"}]},
                    {"A": (1, 20), "B": (2, 21)},
                    len(events) + 1,
                    -1,
                    case,
                )
            ]
            discoveries = {1: (events[0], {"A": (1, 20), "B": (2, 21)})}
            return events, reads, discoveries, transactions

        for opcode, case, state in (
            (3, "metadata_zero_entry", 3),
            (7, "metadata_unknown_enable_update_enabling", 3),
            (7, "metadata_update_streaming", 4),
        ):
            with self.subTest(case=case):
                for marker in (True, False):
                    _metadata_echo(*build(opcode, case, state, marker))
                for tamper, read in ((True, True), (False, False)):
                    with self.assertRaises(TraceError):
                        _metadata_echo(
                            *build(opcode, case, state, False, tamper=tamper, read=read)
                        )

    def test_helper_cp_rejects_duplicate_ase_ids(self):
        event = _events(
            b"d_01: @00:00:00.000001  ASCS_CP transaction=0 generation=1 "
            b"raw=0202010000010000\n",
            "01",
        )
        with self.assertRaises(TraceError):
            _helper_cp(event, {1: (event[0], {"A": (1, 20), "B": (2, 21)})})
        valid = _events(
            b"d_01: @00:00:00.000001  ASCS_CP transaction=0 generation=1 "
            b"raw=0202010000020000\n",
            "01",
        )
        _helper_cp(valid, {1: (valid[0], {"A": (1, 20), "B": (2, 21)})})

    def test_execution_without_trace_is_not_accepted(self):
        record = self.make_record()
        self.assertEqual(
            check_execution_record(
                json.dumps(record).encode(),
                self.policy,
                FAMILY,
                RUN,
                record["logs"]["client"]["path"],
                record["logs"]["receiver"]["path"],
            )["run_id"],
            RUN,
        )
        new = self.client.replace(b"raw=ff01", b"raw=fe01", 1)
        path = Path(record["logs"]["client"]["path"])
        path.write_bytes(new)
        record["logs"]["client"].update(bytes=len(new), sha256=digest(new))
        record["participants"]["client"].update(
            bytes_logged=len(new), log_sha256=digest(new)
        )
        result = self.cli(record)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertFalse(json.loads(result.stdout)["accepted"])

    def test_raw_trace_negative_controls(self):
        patterns = [
            (b"raw=ff01", b"raw=fe01"),
            (b"raw=ffff000100", b"raw=ffff000101"),
            (b"sends=30", b"sends=29"),
            (b"ret=0 att_error=0", b"ret=0 att_error=1"),
            (b"raw=0100", b"raw=0101"),
        ]
        for before, after in patterns:
            with self.subTest(before=before):
                self.assertIn(before, self.client)
                with self.assertRaises(TraceError):
                    check_family_logs(
                        self.policy,
                        FAMILY,
                        self.client.replace(before, after, 1),
                        self.receiver,
                    )
        with self.assertRaises(TraceError):
            check_family_logs(
                self.policy,
                FAMILY,
                self.client,
                self.receiver.replace(b"pushes=27", b"pushes=0", 1),
            )
        for name, mutant in (
            (
                "case step",
                self.client.replace(
                    b"CASE_END name=control_missing_count step=7",
                    b"CASE_END name=control_missing_count step=8",
                    1,
                ),
            ),
            (
                "stale close",
                self.client.replace(
                    b"ASCS_CP_CLOSE generation=1", b"ASCS_CP_CLOSE generation=2", 1
                ),
            ),
            (
                "reused phase",
                self.client.replace(
                    b"ASCS_RECOVERY phase=1", b"ASCS_RECOVERY phase=2", 1
                ),
            ),
            (
                "unlisted warning",
                self.client + b"d_01: @00:00:59.000000  <wrn> other: unlisted\n",
            ),
        ):
            with self.subTest(name=name):
                self.assertNotEqual(mutant, self.client)
                with self.assertRaises(TraceError):
                    check_family_logs(self.policy, FAMILY, mutant, self.receiver)

    def test_public_procedure_ledger_rejects_unowned_and_corrupted_helpers(self):
        lines = self.client.splitlines(keepends=True)
        helper = next(
            i for i, line in enumerate(lines) if b"ASCS_CP transaction=0 " in line
        )
        raw = next(
            i
            for i, line in enumerate(lines)
            if b"ASCS_PROCEDURE_BEGIN " in line and b"owner=raw " in line
        )
        raw_cp = next(
            i for i, line in enumerate(lines) if b"ASCS_CP transaction=1 " in line
        )
        raw_end = next(
            i
            for i, line in enumerate(lines)
            if b"ASCS_PROCEDURE_END " in line and b"owner=raw " in line
        )
        self.assertIn(b"raw=0101010000", lines[helper])
        qos = next(
            i
            for i, line in enumerate(lines)
            if b"ASCS_CP transaction=0 " in line and b"raw=0202010000020000" in line
        )

        def outside_window(copy):
            cp = copy[helper]
            end = copy[helper + 1]
            self.assertIn(b"ASCS_PROCEDURE_END", end)
            # Give the moved CP END's timestamp: timestamp parsing still
            # succeeds, and procedure ownership must reject the late reply.
            copy[helper] = end
            copy[helper + 1] = end.split(b"  ", 1)[0] + b"  " + cp.split(b"  ", 1)[1]

        controls = {
            "missing helper CPP": lambda copy: copy.pop(helper),
            "duplicated helper CPP": lambda copy: copy.insert(helper, copy[helper]),
            "CPP outside owner window": outside_window,
            "wrong helper opcode": lambda copy: copy.__setitem__(
                helper, copy[helper].replace(b"raw=0101010000", b"raw=0201010000")
            ),
            "wrong helper ID": lambda copy: copy.__setitem__(
                helper, copy[helper].replace(b"raw=0101010000", b"raw=0101030000")
            ),
            "wrong helper code": lambda copy: copy.__setitem__(
                helper, copy[helper].replace(b"raw=0101010000", b"raw=0101010100")
            ),
            "wrong QoS helper order": lambda copy: copy.__setitem__(
                qos, copy[qos].replace(b"raw=0202010000020000", b"raw=0202020000010000")
            ),
            "wrong procedure": lambda copy: copy.__setitem__(
                helper, copy[helper].replace(b"procedure=", b"procedure=99", 1)
            ),
            "stale generation": lambda copy: copy.__setitem__(
                helper, copy[helper].replace(b"generation=1", b"generation=2", 1)
            ),
            "wrong raw CP owner": lambda copy: copy.__setitem__(
                raw_cp, copy[raw_cp].replace(b"owner=raw", b"owner=helper", 1)
            ),
            "missing raw BEGIN": lambda copy: copy.pop(raw),
            "missing raw END": lambda copy: copy.pop(raw_end),
        }
        for name, change in controls.items():
            with self.subTest(name=name):
                copy = lines.copy()
                change(copy)
                mutated = b"".join(copy)
                self.assertNotEqual(mutated, self.client)
                with self.assertRaises(TraceError):
                    check_family_logs(self.policy, FAMILY, mutated, self.receiver)

        # Ownership failure must survive correct file/owner hashes through CLI.
        record = self.make_record()
        altered = lines.copy()
        altered.pop(helper)
        data = b"".join(altered)
        Path(record["logs"]["client"]["path"]).write_bytes(data)
        record["logs"]["client"].update(bytes=len(data), sha256=digest(data))
        record["participants"]["client"].update(
            bytes_logged=len(data), log_sha256=digest(data)
        )
        result = self.cli(record)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertFalse(json.loads(result.stdout)["accepted"])

    def test_inventory_mutations_fail_with_recomputed_hash(self):
        for path, value in (
            ("profile", {**self.policy["profile"], "mtu": True}),
            ("totals", {**self.policy["totals"], "cases": True}),
            ("exclusions", ["fake_skip"]),
        ):
            with self.subTest(field=path):
                candidate = copy.deepcopy(self.policy)
                candidate[path] = value
                raw = json.dumps(candidate).encode()
                with self.assertRaises(TraceError):
                    load_inventory(raw, digest(raw))


if __name__ == "__main__":
    unittest.main()
