"""Receiver console parsing and warning scanning for the system HIL
runner (RH2).

Owns the strict regexes for receiver identity and stream summary, the
prompt-bounded receiver shell command runner, the FLPR handshake block
parser, the shared audio/offload parsers (scripts/flpr_status.py), and
the warning scanner applied to both source unprefixed lines and the full
receiver raw log.

The receiver shell uses the default ``uart:~$ `` prompt with echo; a
command's output is a bounded transcript window terminated by the return
of the prompt after the command echo.  Every raw byte is captured to the
receiver log before parsing.
"""

import re

from hil.serial_io import SerialConsoleCancelled, SerialConsoleError, strip_vt100

# Shared audio/offload parsers (scripts/flpr_status.py is the single
# source of truth for the console status block grammar).  Re-exported so
# the runner consumes one receiver module.
from flpr_status import (  # noqa: E402
    parse_audio_faults as _parse_audio_faults,
    parse_offload_status as _parse_offload_status,
)

#: Default Zephyr shell prompt for the receiver build.
RECEIVER_PROMPT = "uart:~$ "

#: Exact receiver identity line (bt_addr_le_to_str renders
#: ``XX:XX:XX:XX:XX:XX (public|random)`` with literal parentheses).
RE_IDENTITY = re.compile(
    r"^Identity: ([0-9A-F]{2}(:[0-9A-F]{2}){5}) \((public|random)\)$"
)

#: Exact receiver-side persisted-bond inventory line (`bt bonds`).
RE_BOND_COUNT = re.compile(r"^Bond count: (\d+)$")

#: Per-stream summary payload (src/bt_bap.c teardown log). LOG_INF adds a
#: backend-specific prefix, so retain only stable payload as a search pattern.
RE_STREAM_SUMMARY = re.compile(
    r"Stream\[(\d+)\] summary: SDUs=(\d+) decoded=(\d+) plc=(\d+) "
    r"decode_err=(\d+) i2s_underrun=(\d+) stream_reset=(\d+) empty_sdu=(\d+)"
    r"(?: rx_valid=(\d+) rx_error=(\d+) rx_lost=(\d+) rx_unknown=(\d+) rx_no_ts=(\d+))?"
    r"(?:\r?$)",
    re.MULTILINE,
)

#: Exact `bt iso quality` shell grammar. Unlike log summaries, these lines
#: have no backend prefix because they are shell output.
RE_ISO_LINK_QUALITY_HEADER = re.compile(r"^--- ISO link quality ---$")
RE_ISO_LINK_QUALITY_STREAM = re.compile(
    r"^  Stream\[([0-9]+)\] handle=0x([0-9A-F]{4}) "
    r"tx_unacked=([0-9]+) tx_flushed=([0-9]+) "
    r"tx_last_subevent=([0-9]+) retransmitted=([0-9]+) "
    r"crc_error=([0-9]+) rx_unreceived=([0-9]+) duplicate=([0-9]+) "
    r"iso_interval_1250us=([0-9]+) nse=([0-9]+) "
    r"cig_sync_us=([0-9]+) cis_sync_us=([0-9]+) "
    r"c_max_pdu=([0-9]+) c_phy=([0-9]+) c_bn=([0-9]+) "
    r"c_flush_1250us=([0-9]+)$"
)
RE_ISO_LINK_QUALITY_STREAM_CANDIDATE = re.compile(r"^[ \t]*Stream\[")

#: FLPR handshake block (src/flpr_shell.c `flpr status`).
RE_HANDSHAKE_HEADER = re.compile(r"^--- FLPR handshake ---$")
RE_HS_BOOL = re.compile(r"^  (Ready|ACKed|Healthy)\s*:\s*(yes|no)$")
RE_HS_ERRORS = re.compile(r"^  Errors\s*:\s*len=(\d+) ver=(\d+) unk=(\d+) send=(\d+)$")
RE_HS_RX_LOST = re.compile(r"^  RX lost\s*:\s*(\d+)$")
RE_HS_RX_DUP = re.compile(r"^  RX dup\s*:\s*(\d+)$")
RE_HS_RX_OOO = re.compile(r"^  RX ooo\s*:\s*(\d+)$")
RE_HS_RX_MISSED = re.compile(r"^  RX missed\s*:\s*(\d+)$")

# Acceptance-diagnostic shell acknowledgements. Keep raw console wording in
# evidence; runner maps these exact accepted forms to stable recovery tokens.
RE_FAULT_HANG_ACK = re.compile(r"\bFAULT_HANG_ACK received\b")
RE_TIMED_STALL_ACK = re.compile(
    r"\bFLPR timed stall applied:\s*bits=0x01\s+duration=60\s+ms\b"
)

#: Warning signatures that fail the row anywhere in a source/raw log.
WARNING_PATTERNS = (
    re.compile(r"LOG_WRN"),
    re.compile(r"LOG_ERR"),
    re.compile(r"(?i)<wrn>"),
    re.compile(r"(?i)<err>"),
    re.compile(r"FATAL"),
    re.compile(r"(?i)\bassert(ion)?\b"),
    re.compile(r"(?i)\bfault\b"),
    re.compile(r"(?i)stack overflow"),
    re.compile(r"i2s_nrfx: Next buffers not supplied on time"),
    re.compile(r"Cannot write in state"),
    re.compile(r"(?i)ISO sequence discontinuity"),
    re.compile(r"(?i)unexpected recovery"),
)

# The hang row may retain only these recovery diagnostics, and only inside the
# runner-recorded raw-byte recovery window. The envelope accepts one live shell
# prompt plus normal Zephyr text-log prefixes while keeping both module name and
# payload exact.
RE_HANG_RECOVERY_WARNING_ENVELOPE = re.compile(
    r"^(?:" + re.escape(RECEIVER_PROMPT) + r")?"
    r"(?:\[[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3},[0-9]{3}\][ ]+)?"
    r"(?:<wrn>|LOG_WRN):?[ ]+"
    r"(?P<module>audio_offload|flpr_ring):[ ]+(?P<payload>.*)$"
)
HANG_RECOVERY_WARNING_PAYLOADS = (
    (
        "audio_offload",
        re.compile(r"^offload: heartbeat supervisor → RECOVERING$"),
    ),
    (
        "audio_offload",
        re.compile(
            r"^offload recovery: handshake unhealthy, escalating to runtime restart$"
        ),
    ),
    (
        "audio_offload",
        re.compile(
            r"^offload recovery: short ring reset failed \(-116\), "
            r"escalating to runtime restart$"
        ),
    ),
    (
        "flpr_ring",
        re.compile(r"^RING_RESET_ACK timeout \(100 ms\)$"),
    ),
)

_LINE_ENDINGS = "\n\r\v\f\x1c\x1d\x1e\x85\u2028\u2029"

#: Frozen receiver transport limits (docs/development/system-hil-milestones.md,
#: "Receiver transport limits (frozen 2026-09-03)").  Basis: adjacent boards,
#: 2M PHY, RTN 5; healthy 10 ms hardware delivers essentially every submitted
#: SDU (RH2 formal pass: source sub=764, receiver SDUs=764, plc=13/777=1.7%).
#: Values are plan-of-record constants: changing one is a plan revision, not a
#: test tweak.
RX_VALID_RATIO_FLOOR = 0.90  # fraction of expected submitted SDUs per stream
PLC_RATIO_CEILING = 0.05  # fraction of decoded frames


def validate_stream_transport(summary, expected_submitted):
    """Validate one receiver stream summary against frozen transport limits.

    ``expected_submitted`` is the row's expected submitted SDU count for one
    segment (``RowSpec.expected_submitted_per_segment``).  Returns a list of
    violation strings; empty means pass.  Extended summary fields that are
    missing (``None``) fail closed: a firmware image that does not emit them
    cannot pass a healthy row.  ``rx_lost`` and ``rx_no_ts`` are record-only
    per the plan and never gate.

    Pure host function: no I/O, no state, suitable for direct unit testing.
    """
    violations = []

    rx_valid = summary.get("rx_valid")
    if rx_valid is None:
        violations.append("rx_valid missing (firmware summary without extended fields)")
    else:
        floor = RX_VALID_RATIO_FLOOR * expected_submitted
        if rx_valid < floor:
            violations.append(
                "rx_valid=%d below floor: need >= %d (90%% of %d submitted)"
                % (rx_valid, int(floor), expected_submitted)
            )

    for field in ("rx_error", "rx_unknown", "empty_sdu"):
        value = summary.get(field)
        if value is None:
            violations.append("%s missing (extended fields required)" % field)
        elif value != 0:
            violations.append("%s=%d (must be 0)" % (field, value))

    decoded = summary.get("decoded")
    plc = summary.get("plc")
    if decoded is None or plc is None:
        # decoded/plc are core summary fields; a None here means the parser
        # did not see a well-formed summary at all.  Treat as closed.
        violations.append("decoded/plc missing from stream summary")
    elif decoded > 0 and plc > PLC_RATIO_CEILING * decoded:
        violations.append(
            "plc=%d above ceiling: need <= %d (5%% of decoded=%d)"
            % (plc, int(PLC_RATIO_CEILING * decoded), decoded)
        )

    return violations


#: Explicit shell error output. Keep this limited to command-window evidence:
#: normal firmware logs can contain words such as "error" in labels or
#: diagnostic prose, while every shell failure must use ``shell_error()``.
RE_SHELL_ERROR = re.compile(
    r"(?:^Error:|^error:|:\s*command not found\b)", re.IGNORECASE
)

# Callback-timed HCI Remove ISO Path trace payloads. Logger prefixes are
# intentionally outside these expressions because backend formatting varies.
RE_HCI_REMOVE_ISO_PATH_TRACE_ARM = re.compile(
    r"HCI remove ISO path trace armed:\s*"
    r"core_id=(?P<core_id>[+-]?\d+)\s+"
    r"driver_id=(?P<driver_id>[+-]?\d+)\s+"
    r"core_level=(?P<core_level>[+-]?\d+)\s+"
    r"driver_level=(?P<driver_level>[+-]?\d+)"
)
RE_HCI_REMOVE_ISO_PATH_TRACE_ARM_FAILED = re.compile(
    r"HCI remove ISO path trace arm failed:\s*"
    r"err=(?P<err>[+-]?\d+)\s+"
    r"core_id=(?P<core_id>[+-]?\d+)\s+"
    r"driver_id=(?P<driver_id>[+-]?\d+)\s+"
    r"core_level=(?P<core_level>[+-]?\d+)\s+"
    r"driver_level=(?P<driver_level>[+-]?\d+)"
)
RE_HCI_REMOVE_ISO_PATH_TRACE_DROPPED = re.compile(
    r"^---\s*(?P<count>\d+)\s+messages dropped\s*---$"
)
RE_HCI_CORE_TRACE_SEND = re.compile(
    r"(?:Sending command 0x206f\b|opcode 0x206f\s+param_len\b|"
    r"buf\s+\S+\s+opcode 0x206f\s+len\b)",
    re.IGNORECASE,
)
RE_HCI_SDC_TRACE_COMPLETE = re.compile(r"Command Complete\s+\(0x206f\b", re.IGNORECASE)
RE_HCI_SDC_TRACE_STATUS = re.compile(r"Command Status\s+\(0x206f\b", re.IGNORECASE)
RE_HCI_CORE_TRACE_COMPLETE = re.compile(
    r"(?:opcode\s+0x206f\b.*\bstatus\b|"
    r"(?:complete|completed|done).*0x206f\b|"
    r"0x206f\b.*(?:complete|completed|done))",
    re.IGNORECASE,
)
RE_HCI_TRACE_TIMEOUT_FATAL = re.compile(
    r"(?:(?:timeout|timed out|unresponsive|fatal).*0x206f\b|"
    r"0x206f\b.*(?:timeout|timed out|unresponsive|fatal))",
    re.IGNORECASE,
)

# Direct SDC wrapper trace payloads. Logger prefixes are intentionally outside
# these expressions because backend formatting varies. Unlike the older HCI
# trace, this diagnostic has no runtime-filter or driver-log grammar.
SDC_HCI_REMOVE_ISO_PATH_TRACE_ARM_TEXT = "SDC LE Remove ISO Data Path trace armed"
SDC_HCI_REMOVE_ISO_PATH_TRACE_ENTRY_TEXT = "SDC LE Remove ISO Data Path trace entry"
SDC_HCI_REMOVE_ISO_PATH_TRACE_RETURN_PREFIX = (
    "SDC LE Remove ISO Data Path trace return: status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RETURN = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_RETURN_PREFIX) + r"0x([0-9a-f]{2})$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path hci_internal_cmd_put entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_RETURN_PREFIX = (
    "SDC LE Remove ISO Data Path hci_internal_cmd_put return: status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_RETURN = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_RETURN_PREFIX) + r"([+-]?[0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_LOCK_RELEASE_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path multithreading_lock_release entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_LOCK_RELEASE_RETURN_TEXT = (
    "SDC LE Remove ISO Data Path multithreading_lock_release return"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path k_work_submit_to_queue entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_RETURN_PREFIX = (
    "SDC LE Remove ISO Data Path k_work_submit_to_queue return: status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_RETURN = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_RETURN_PREFIX)
    + r"([+-]?[0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_TEXT = (
    "SDC LE Remove ISO Data Path work state snapshot scheduled"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_TEXT + ": status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_PREFIX)
    + r"([+-]?[0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_TEXT = (
    "SDC LE Remove ISO Data Path work state snapshot"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_TEXT + ": busy="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_PREFIX)
    + r"0x([0-9a-fA-F]+) mpsl_state=([A-Za-z0-9_+]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_TEXT = (
    "SDC LE Remove ISO Data Path scheduler unlock trace armed"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_EXTENDED_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_TEXT + ": queue_is_mpsl="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_EXTENDED = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_EXTENDED_PREFIX)
    + r"([01])$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path k_sched_unlock entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_RETURN_TEXT = (
    "SDC LE Remove ISO Data Path k_sched_unlock return"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_TEXT = (
    "SDC LE Remove ISO Data Path k_sched_unlock state"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_TEXT + ": busy="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_LEGACY = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_PREFIX)
    + r"0x([0-9a-fA-F]+) mpsl_state=([A-Za-z0-9_+]+)$"
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_V9 = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_PREFIX)
    + r"0x([0-9a-fA-F]+) mpsl_state=([A-Za-z0-9_+]+)"
    r" resumed_current_is_sender=([01]) mpsl_switches_during_unlock=([0-9]+)"
    r" sender_prio=([+-]?[0-9]+) resumed_current_prio=([+-]?[0-9]+)"
    r" mpsl_prio=([+-]?[0-9]+|unavailable)$"
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_EXTENDED = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_PREFIX)
    + r"0x([0-9a-fA-F]+) mpsl_state=([A-Za-z0-9_+]+)"
    r" current_is_sender=([01]) mpsl_is_current=([01])"
    r" sender_prio=([+-]?[0-9]+) current_prio=([+-]?[0-9]+)"
    r" mpsl_prio=([+-]?[0-9]+|unavailable)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_ARM_TEXT = (
    "SDC LE Remove ISO Data Path post-unlock yield trace armed"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path post-unlock yield entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_RETURN_TEXT = (
    "SDC LE Remove ISO Data Path post-unlock yield return"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE_TEXT = (
    "SDC LE Remove ISO Data Path post-unlock yield state"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE_TEXT + ": busy="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE_PREFIX)
    + r"0x([0-9a-fA-F]+) mpsl_state=([A-Za-z0-9_+]+)"
    r" resumed_current_is_sender=([01]) mpsl_switches_during_yield=([0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path hci_internal_msg_get entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_RETURN_PREFIX = (
    "SDC LE Remove ISO Data Path hci_internal_msg_get return: status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_RETURN = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_RETURN_PREFIX) + r"([+-]?[0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_COMPLETION_PREFIX = (
    "SDC LE Remove ISO Data Path completion: status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_COMPLETION = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_COMPLETION_PREFIX) + r"0x([0-9a-f]{2})$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM_TEXT = (
    "SDC LE Remove ISO Data Path receive disposition trace armed"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM_TEXT + ": queue_is_mpsl="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM_PREFIX) + r"([01])$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_ENTRY_TEXT = (
    "SDC LE Remove ISO Data Path receive disposition fetch entry"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_RETURN_PREFIX = (
    "SDC LE Remove ISO Data Path receive disposition fetch return: status="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_RETURN = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_RETURN_PREFIX)
    + r"([+-]?[0-9]+) msg_type=([0-9]+|na)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ALLOCATION_PREFIX = (
    "SDC LE Remove ISO Data Path receive disposition allocation: "
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_EVT_ALLOCATION = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ALLOCATION_PREFIX)
    + r"kind=evt evt=0x([0-9a-fA-F]{2}) discardable=([01]) "
    r"buffer_available=([01]) target_busy=0x([0-9a-fA-F]+)$"
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_RX_ALLOCATION = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ALLOCATION_PREFIX)
    + r"kind=rx type=([0-9]+) buffer_available=([01]) "
    r"target_busy=0x([0-9a-fA-F]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM_TEXT = (
    "SDC LE Remove ISO Data Path ISO RX lifetime trace armed"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM_TEXT + ": capacity="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM_PREFIX) + r"([0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT_TEXT = (
    "SDC LE Remove ISO Data Path ISO RX lifetime snapshot"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT_TEXT + ": reason="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT_PREFIX)
    + r"(stream_start|disable|unavailable) capacity=([0-9]+) "
    r"outstanding=([0-9]+) high_water=([0-9]+) allocations=([0-9]+) "
    r"final_unrefs=([0-9]+) callbacks_active=([0-9]+) callbacks_total=([0-9]+)$"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_TEXT = (
    "SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_TEXT
    + ": reason="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA19 = (
    re.compile(
        re.escape(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX
        )
        + r"(disable|unavailable) undispatched=([0-9]+) host_dispatched=([0-9]+) "
        r"tx_notify_flush_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_pend_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_pend_thread_marked_pending=([0-9]+) "
        r"tx_notify_flush_semaphore_give_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_pend_returned=([0-9]+) "
        r"tx_notify_flush_semaphore_returned=([0-9]+) "
        r"tx_notify_flush_returned=([0-9]+) host_returned=([0-9]+) "
        r"app_callback_seen=([0-9]+) unclassified=([0-9]+)$"
    )
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA18 = (
    re.compile(
        re.escape(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX
        )
        + r"(disable|unavailable) undispatched=([0-9]+) host_dispatched=([0-9]+) "
        r"tx_notify_flush_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_pend_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_pend_returned=([0-9]+) "
        r"tx_notify_flush_semaphore_returned=([0-9]+) "
        r"tx_notify_flush_returned=([0-9]+) host_returned=([0-9]+) "
        r"app_callback_seen=([0-9]+) unclassified=([0-9]+)$"
    )
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA17 = (
    re.compile(
        re.escape(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX
        )
        + r"(disable|unavailable) undispatched=([0-9]+) host_dispatched=([0-9]+) "
        r"tx_notify_flush_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_entered=([0-9]+) "
        r"tx_notify_flush_semaphore_returned=([0-9]+) "
        r"tx_notify_flush_returned=([0-9]+) host_returned=([0-9]+) "
        r"app_callback_seen=([0-9]+) unclassified=([0-9]+)$"
    )
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX)
    + r"(disable|unavailable) undispatched=([0-9]+) host_dispatched=([0-9]+) "
    r"tx_notify_flush_entered=([0-9]+) tx_notify_flush_returned=([0-9]+) "
    r"host_returned=([0-9]+) app_callback_seen=([0-9]+) unclassified=([0-9]+)$"
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA15 = (
    re.compile(
        re.escape(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX
        )
        + r"(disable|unavailable) undispatched=([0-9]+) host_dispatched=([0-9]+) "
        r"host_returned=([0-9]+) app_callback_seen=([0-9]+) unclassified=([0-9]+)$"
    )
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA14 = (
    re.compile(
        re.escape(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX
        )
        + r"(disable|unavailable) undispatched=([0-9]+) host_dispatched=([0-9]+) "
        r"app_callback_seen=([0-9]+) unclassified=([0-9]+)$"
    )
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE_TEXT = (
    "SDC LE Remove ISO Data Path ISO RX lifetime first free after unavailable"
)
SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE_PREFIX = (
    SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE_TEXT
    + ": outstanding_before="
)
RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE = re.compile(
    re.escape(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE_PREFIX)
    + r"([0-9]+) callbacks_active=([0-9]+) allocations=([0-9]+) final_unrefs=([0-9]+)$"
)


class ReceiverError(Exception):
    """Raised for any receiver parsing or command boundary failure."""


def parse_audio_faults(text):
    """Parse audio counters after removing Zephyr VT100 presentation codes."""
    return _parse_audio_faults(strip_vt100(text))


def parse_offload_status(text):
    """Parse offload counters after removing Zephyr VT100 presentation codes."""
    return _parse_offload_status(strip_vt100(text))


def parse_identity(text):
    """Parse the exact identity line from a transcript; None when no
    line matches."""
    for line in strip_vt100(text).splitlines():
        match = RE_IDENTITY.match(line.strip())
        if match:
            return {
                "address": match.group(1),
                "address_type": match.group(3),
            }
    return None


def parse_bond_count(text):
    """Parse exact receiver bond inventory; None means missing/malformed."""
    for line in strip_vt100(text).splitlines():
        match = RE_BOND_COUNT.match(line.strip())
        if match:
            return int(match.group(1))
    return None


def _hci_remove_iso_path_trace_record(start_offset, end_offset, line, **fields):
    record = {
        "start_offset": start_offset,
        "end_offset": end_offset,
        "line": line,
    }
    record.update(fields)
    return record


def parse_hci_remove_iso_path_trace(raw):
    """Parse and validate callback-timed HCI Remove ISO Path evidence.

    The parser consumes retained receiver bytes, not shell transcripts. Every
    recognized record keeps its raw byte range and VT100-stripped line. Parser
    and validation errors remain in the returned object so failed rows retain
    the same evidence as passing rows.
    """
    payload = {
        "schema_version": 1,
        "arm_markers": [],
        "arm_failure_markers": [],
        "dropped_messages": [],
        "core_sends": [],
        "driver_command_complete": [],
        "driver_command_status": [],
        "core_completion_done": [],
        "host_timeout_fatal": [],
        "parser_errors": [],
        "validation_errors": [],
    }

    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    elif not isinstance(raw, (bytes, bytearray, memoryview)):
        payload["parser_errors"].append("receiver trace evidence is not bytes")
        payload["validation_errors"].append("receiver trace evidence is not bytes")
        return payload
    raw = bytes(raw)

    try:
        raw_lines = tuple(iter_raw_utf8_lines(raw))
    except UnicodeDecodeError as exc:
        message = "receiver trace evidence invalid UTF-8: %s" % exc
        payload["parser_errors"].append(message)
        payload["validation_errors"].append(message)
        raw_lines = ()

    first_arm_seen = False
    for start_offset, end_offset, raw_line, _complete in raw_lines:
        line = strip_vt100(raw_line).strip()

        if "HCI remove ISO path trace armed:" in line:
            match = RE_HCI_REMOVE_ISO_PATH_TRACE_ARM.search(line)
            if match is None:
                payload["parser_errors"].append("malformed arm marker: %s" % line)
            else:
                first_arm_seen = True
                payload["arm_markers"].append(
                    _hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        core_id=int(match.group("core_id")),
                        driver_id=int(match.group("driver_id")),
                        core_level=int(match.group("core_level")),
                        driver_level=int(match.group("driver_level")),
                    )
                )

        if "HCI remove ISO path trace arm failed:" in line:
            match = RE_HCI_REMOVE_ISO_PATH_TRACE_ARM_FAILED.search(line)
            if match is None:
                payload["parser_errors"].append(
                    "malformed arm-failure marker: %s" % line
                )
            else:
                payload["arm_failure_markers"].append(
                    _hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        err=int(match.group("err")),
                        core_id=int(match.group("core_id")),
                        driver_id=int(match.group("driver_id")),
                        core_level=int(match.group("core_level")),
                        driver_level=int(match.group("driver_level")),
                    )
                )

        if "messages dropped" in line:
            dropped_match_line = line
            if dropped_match_line.startswith(RECEIVER_PROMPT):
                dropped_match_line = dropped_match_line[len(RECEIVER_PROMPT) :]
            match = RE_HCI_REMOVE_ISO_PATH_TRACE_DROPPED.fullmatch(dropped_match_line)
            if match is None:
                payload["parser_errors"].append(
                    "malformed dropped-message record: %s" % line
                )
            else:
                payload["dropped_messages"].append(
                    _hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        count=int(match.group("count")),
                        classification="after_arm" if first_arm_seen else "before_arm",
                    )
                )

        if "bt_hci_core" in line and RE_HCI_CORE_TRACE_SEND.search(line):
            payload["core_sends"].append(
                _hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification="after_arm" if first_arm_seen else "before_arm",
                )
            )

        if "bt_sdc_hci_driver" in line and RE_HCI_SDC_TRACE_COMPLETE.search(line):
            payload["driver_command_complete"].append(
                _hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification="after_arm" if first_arm_seen else "before_arm",
                )
            )

        if "bt_sdc_hci_driver" in line and RE_HCI_SDC_TRACE_STATUS.search(line):
            payload["driver_command_status"].append(
                _hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification="after_arm" if first_arm_seen else "before_arm",
                )
            )

        if "bt_hci_core" in line and RE_HCI_CORE_TRACE_COMPLETE.search(line):
            payload["core_completion_done"].append(
                _hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification="after_arm" if first_arm_seen else "before_arm",
                )
            )

        if RE_HCI_TRACE_TIMEOUT_FATAL.search(line):
            payload["host_timeout_fatal"].append(
                _hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification="after_arm" if first_arm_seen else "before_arm",
                )
            )

    validation_errors = payload["validation_errors"]
    validation_errors.extend(payload["parser_errors"])
    if len(payload["arm_markers"]) != 1:
        validation_errors.append(
            "expected exactly one successful arm marker, found %d"
            % len(payload["arm_markers"])
        )
    if payload["arm_markers"]:
        arm = payload["arm_markers"][0]
        for field in ("core_id", "driver_id"):
            if arm[field] < 0:
                validation_errors.append("arm marker %s is negative" % field)
        for field in ("core_level", "driver_level"):
            if arm[field] != 4:
                validation_errors.append(
                    "arm marker %s is %d, expected 4" % (field, arm[field])
                )
    if payload["arm_failure_markers"]:
        validation_errors.append("arm-failure marker present")
    for record in payload["dropped_messages"]:
        if record["classification"] == "after_arm":
            validation_errors.append("dropped messages after arm: %d" % record["count"])
    if not any(
        record["classification"] == "after_arm" for record in payload["core_sends"]
    ):
        validation_errors.append("missing post-arm bt_hci_core 0x206f send record")

    return payload


def _sdc_hci_remove_iso_path_trace_record(
    start_offset, end_offset, line, classification, **fields
):
    record = {
        "start_offset": start_offset,
        "end_offset": end_offset,
        "line": line,
        "classification": classification,
    }
    record.update(fields)
    return record


def _sdc_hci_marker_candidate(line):
    """Return SDC marker text after removing at most one shell prompt."""
    candidate = line
    if candidate.startswith(RECEIVER_PROMPT):
        candidate = candidate[len(RECEIVER_PROMPT) :]
        if candidate.startswith(RECEIVER_PROMPT):
            return None
    return candidate


def _sdc_hci_marker_state(line, marker):
    """Return ``None`` for unrelated, ``True`` for exact, ``False`` malformed."""
    if marker not in line:
        return None
    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None or marker not in candidate:
        return False
    if candidate.count(marker) != 1 or not candidate.endswith(marker):
        return False
    return True


def _sdc_hci_scheduler_unlock_arm_state(line):
    """Return ``(state, queue_is_mpsl)`` for a scheduler-arm marker."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    extended_match = (
        RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_EXTENDED.search(candidate)
    )
    extended_state = (
        extended_match is not None
        and candidate.count(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_EXTENDED_PREFIX
        )
        == 1
        and extended_match.end() == len(candidate)
    )
    if extended_state and extended_match is not None:
        return True, int(extended_match.group(1))

    legacy_state = candidate.count(
        SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_TEXT
    ) == 1 and candidate.endswith(
        SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ARM_TEXT
    )
    if legacy_state:
        return True, None
    return False, None


def _sdc_hci_scheduler_unlock_state(line):
    """Return ``(state, fields)`` for a scheduler-state marker."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    prefix_count = candidate.count(
        SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_PREFIX
    )
    extended_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_V9.search(
        candidate
    )
    if (
        extended_match is not None
        and prefix_count == 1
        and extended_match.end() == len(candidate)
    ):
        mpsl_prio = extended_match.group(7)
        return True, {
            "busy": int(extended_match.group(1), 16),
            "mpsl_state": extended_match.group(2),
            "resumed_current_is_sender": bool(int(extended_match.group(3))),
            "mpsl_switches_during_unlock": int(extended_match.group(4), 10),
            "sender_prio": int(extended_match.group(5), 10),
            "resumed_current_prio": int(extended_match.group(6), 10),
            "mpsl_prio": None if mpsl_prio == "unavailable" else int(mpsl_prio, 10),
        }

    extended_match = (
        RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_EXTENDED.search(
            candidate
        )
    )
    if (
        extended_match is not None
        and prefix_count == 1
        and extended_match.end() == len(candidate)
    ):
        mpsl_prio = extended_match.group(7)
        return True, {
            "busy": int(extended_match.group(1), 16),
            "mpsl_state": extended_match.group(2),
            "current_is_sender": bool(int(extended_match.group(3))),
            "mpsl_is_current": bool(int(extended_match.group(4))),
            "sender_prio": int(extended_match.group(5), 10),
            "current_prio": int(extended_match.group(6), 10),
            "mpsl_prio": None if mpsl_prio == "unavailable" else int(mpsl_prio, 10),
        }

    legacy_match = (
        RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_STATE_LEGACY.search(candidate)
    )
    if (
        legacy_match is not None
        and prefix_count == 1
        and legacy_match.end() == len(candidate)
    ):
        return True, {
            "busy": int(legacy_match.group(1), 16),
            "mpsl_state": legacy_match.group(2),
        }

    return False, None


def _sdc_hci_scheduler_yield_state(line):
    """Return ``(state, fields)`` for a post-unlock yield state marker."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    state_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE.search(
        candidate
    )
    state = (
        state_match is not None
        and candidate.count(SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_STATE_PREFIX)
        == 1
        and state_match.end() == len(candidate)
    )
    if not state or state_match is None:
        return False, None

    return True, {
        "busy": int(state_match.group(1), 16),
        "mpsl_state": state_match.group(2),
        "resumed_current_is_sender": bool(int(state_match.group(3))),
        "mpsl_switches_during_yield": int(state_match.group(4), 10),
    }


def _sdc_hci_receive_disposition_arm_state(line):
    """Return ``(state, queue_is_mpsl)`` for a receive-disposition arm."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM.search(candidate)
    valid = (
        match is not None
        and candidate.count(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ARM_PREFIX
        )
        == 1
        and match.end() == len(candidate)
    )
    if not valid or match is None:
        return False, None
    return True, int(match.group(1), 10)


def _sdc_hci_receive_disposition_allocation_state(line):
    """Return ``(state, kind, fields)`` for a receive allocation marker."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ALLOCATION_PREFIX not in line:
        return None, None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None, None
    if (
        candidate.count(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ALLOCATION_PREFIX
        )
        != 1
    ):
        return False, None, None

    event_match = (
        RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_EVT_ALLOCATION.search(
            candidate
        )
    )
    if event_match is not None and event_match.end() == len(candidate):
        return (
            True,
            "evt",
            {
                "evt": int(event_match.group(1), 16),
                "discardable": bool(int(event_match.group(2), 10)),
                "buffer_available": bool(int(event_match.group(3), 10)),
                "target_busy": int(event_match.group(4), 16),
            },
        )

    rx_match = (
        RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_RX_ALLOCATION.search(
            candidate
        )
    )
    if rx_match is not None and rx_match.end() == len(candidate):
        return (
            True,
            "rx",
            {
                "type": int(rx_match.group(1), 10),
                "buffer_available": bool(int(rx_match.group(2), 10)),
                "target_busy": int(rx_match.group(3), 16),
            },
        )

    return False, None, None


def _sdc_hci_iso_rx_lifetime_arm_state(line):
    """Return ``(state, capacity)`` for an ISO RX lifetime arm marker."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM.search(candidate)
    valid = (
        match is not None
        and candidate.count(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ARM_PREFIX)
        == 1
        and match.end() == len(candidate)
    )
    if not valid or match is None:
        return False, None
    return True, int(match.group(1), 10)


def _sdc_hci_iso_rx_lifetime_snapshot_state(line):
    """Return ``(state, fields)`` for an ISO RX lifetime snapshot."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT.search(candidate)
    valid = (
        match is not None
        and candidate.count(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_SNAPSHOT_PREFIX
        )
        == 1
        and match.end() == len(candidate)
    )
    if not valid or match is None:
        return False, None
    return True, {
        "reason": match.group(1),
        "capacity": int(match.group(2), 10),
        "outstanding": int(match.group(3), 10),
        "high_water": int(match.group(4), 10),
        "allocations": int(match.group(5), 10),
        "final_unrefs": int(match.group(6), 10),
        "callbacks_active": int(match.group(7), 10),
        "callbacks_total": int(match.group(8), 10),
    }


def _sdc_hci_iso_rx_lifetime_disposition_snapshot_state(line):
    """Return ``(state, fields)`` for an ISO RX lifetime disposition snapshot."""
    if (
        SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_TEXT
        not in line
    ):
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    prefix_count = candidate.count(
        SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_PREFIX
    )
    schema19_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA19.search(
        candidate
    )
    if (
        schema19_match is not None
        and prefix_count == 1
        and schema19_match.end() == len(candidate)
    ):
        return True, {
            "reason": schema19_match.group(1),
            "undispatched": int(schema19_match.group(2), 10),
            "host_dispatched": int(schema19_match.group(3), 10),
            "tx_notify_flush_entered": int(schema19_match.group(4), 10),
            "tx_notify_flush_semaphore_entered": int(schema19_match.group(5), 10),
            "tx_notify_flush_semaphore_pend_entered": int(schema19_match.group(6), 10),
            "tx_notify_flush_semaphore_pend_thread_marked_pending": int(
                schema19_match.group(7), 10
            ),
            "tx_notify_flush_semaphore_give_entered": int(schema19_match.group(8), 10),
            "tx_notify_flush_semaphore_pend_returned": int(schema19_match.group(9), 10),
            "tx_notify_flush_semaphore_returned": int(schema19_match.group(10), 10),
            "tx_notify_flush_returned": int(schema19_match.group(11), 10),
            "host_returned": int(schema19_match.group(12), 10),
            "app_callback_seen": int(schema19_match.group(13), 10),
            "unclassified": int(schema19_match.group(14), 10),
        }

    schema18_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA18.search(
        candidate
    )
    if (
        schema18_match is not None
        and prefix_count == 1
        and schema18_match.end() == len(candidate)
    ):
        return True, {
            "reason": schema18_match.group(1),
            "undispatched": int(schema18_match.group(2), 10),
            "host_dispatched": int(schema18_match.group(3), 10),
            "tx_notify_flush_entered": int(schema18_match.group(4), 10),
            "tx_notify_flush_semaphore_entered": int(schema18_match.group(5), 10),
            "tx_notify_flush_semaphore_pend_entered": int(schema18_match.group(6), 10),
            "tx_notify_flush_semaphore_pend_thread_marked_pending": None,
            "tx_notify_flush_semaphore_give_entered": None,
            "tx_notify_flush_semaphore_pend_returned": int(schema18_match.group(7), 10),
            "tx_notify_flush_semaphore_returned": int(schema18_match.group(8), 10),
            "tx_notify_flush_returned": int(schema18_match.group(9), 10),
            "host_returned": int(schema18_match.group(10), 10),
            "app_callback_seen": int(schema18_match.group(11), 10),
            "unclassified": int(schema18_match.group(12), 10),
        }

    schema17_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA17.search(
        candidate
    )
    if (
        schema17_match is not None
        and prefix_count == 1
        and schema17_match.end() == len(candidate)
    ):
        return True, {
            "reason": schema17_match.group(1),
            "undispatched": int(schema17_match.group(2), 10),
            "host_dispatched": int(schema17_match.group(3), 10),
            "tx_notify_flush_entered": int(schema17_match.group(4), 10),
            "tx_notify_flush_semaphore_entered": int(schema17_match.group(5), 10),
            "tx_notify_flush_semaphore_pend_entered": None,
            "tx_notify_flush_semaphore_pend_thread_marked_pending": None,
            "tx_notify_flush_semaphore_give_entered": None,
            "tx_notify_flush_semaphore_pend_returned": None,
            "tx_notify_flush_semaphore_returned": int(schema17_match.group(6), 10),
            "tx_notify_flush_returned": int(schema17_match.group(7), 10),
            "host_returned": int(schema17_match.group(8), 10),
            "app_callback_seen": int(schema17_match.group(9), 10),
            "unclassified": int(schema17_match.group(10), 10),
        }

    match = (
        RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT.search(
            candidate
        )
    )
    if match is not None and prefix_count == 1 and match.end() == len(candidate):
        return True, {
            "reason": match.group(1),
            "undispatched": int(match.group(2), 10),
            "host_dispatched": int(match.group(3), 10),
            "tx_notify_flush_entered": int(match.group(4), 10),
            "tx_notify_flush_semaphore_entered": None,
            "tx_notify_flush_semaphore_pend_entered": None,
            "tx_notify_flush_semaphore_pend_thread_marked_pending": None,
            "tx_notify_flush_semaphore_give_entered": None,
            "tx_notify_flush_semaphore_pend_returned": None,
            "tx_notify_flush_semaphore_returned": None,
            "tx_notify_flush_returned": int(match.group(5), 10),
            "host_returned": int(match.group(6), 10),
            "app_callback_seen": int(match.group(7), 10),
            "unclassified": int(match.group(8), 10),
        }

    schema15_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA15.search(
        candidate
    )
    if (
        schema15_match is not None
        and prefix_count == 1
        and schema15_match.end() == len(candidate)
    ):
        return True, {
            "reason": schema15_match.group(1),
            "undispatched": int(schema15_match.group(2), 10),
            "host_dispatched": int(schema15_match.group(3), 10),
            "tx_notify_flush_entered": None,
            "tx_notify_flush_semaphore_entered": None,
            "tx_notify_flush_semaphore_pend_entered": None,
            "tx_notify_flush_semaphore_pend_thread_marked_pending": None,
            "tx_notify_flush_semaphore_give_entered": None,
            "tx_notify_flush_semaphore_pend_returned": None,
            "tx_notify_flush_semaphore_returned": None,
            "tx_notify_flush_returned": None,
            "host_returned": int(schema15_match.group(4), 10),
            "app_callback_seen": int(schema15_match.group(5), 10),
            "unclassified": int(schema15_match.group(6), 10),
        }

    schema14_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_SNAPSHOT_SCHEMA14.search(
        candidate
    )
    if (
        schema14_match is not None
        and prefix_count == 1
        and schema14_match.end() == len(candidate)
    ):
        return True, {
            "reason": schema14_match.group(1),
            "undispatched": int(schema14_match.group(2), 10),
            "host_dispatched": int(schema14_match.group(3), 10),
            "tx_notify_flush_entered": None,
            "tx_notify_flush_semaphore_entered": None,
            "tx_notify_flush_semaphore_pend_entered": None,
            "tx_notify_flush_semaphore_pend_thread_marked_pending": None,
            "tx_notify_flush_semaphore_give_entered": None,
            "tx_notify_flush_semaphore_pend_returned": None,
            "tx_notify_flush_semaphore_returned": None,
            "tx_notify_flush_returned": None,
            "host_returned": None,
            "app_callback_seen": int(schema14_match.group(4), 10),
            "unclassified": int(schema14_match.group(5), 10),
        }

    return False, None


def _sdc_hci_iso_rx_lifetime_first_free_state(line):
    """Return ``(state, fields)`` for an ISO RX lifetime first-free marker."""
    if SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE_TEXT not in line:
        return None, None

    candidate = _sdc_hci_marker_candidate(line)
    if candidate is None:
        return False, None

    match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE.search(
        candidate
    )
    valid = (
        match is not None
        and candidate.count(
            SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_FIRST_FREE_PREFIX
        )
        == 1
        and match.end() == len(candidate)
    )
    if not valid or match is None:
        return False, None
    return True, {
        "outstanding_before": int(match.group(1), 10),
        "callbacks_active": int(match.group(2), 10),
        "allocations": int(match.group(3), 10),
        "final_unrefs": int(match.group(4), 10),
    }


def _sdc_hci_receive_disposition_expected_allocation(msg_type):
    """Return exact host-buffer mapping for one supported SDC message type."""
    if msg_type == 2:
        return "rx", 8
    if msg_type == 4:
        return "evt", None
    if msg_type == 8:
        return "rx", 32
    return None


def _sdc_hci_receive_disposition_allocation_matches(msg_type, allocation):
    expected = _sdc_hci_receive_disposition_expected_allocation(msg_type)
    if expected is None or not isinstance(allocation, dict):
        return False
    kind, buffer_type = expected
    if allocation.get("kind") != kind:
        return False
    return kind == "evt" or allocation.get("type") == buffer_type


def parse_sdc_hci_remove_iso_path_trace(raw):
    """Parse and validate direct SDC LE Remove ISO Data Path evidence.

    Every recognized record retains its raw byte range and VT100-stripped
    line. Records before the first arm are retained but cannot satisfy the
    post-arm evidence requirements.
    """
    payload = {
        "schema_version": 19,
        "snapshot_schema_version": 1,
        "receive_disposition_outcome": None,
        "arm_markers": [],
        "dropped_messages": [],
        "cmd_put_entries": [],
        "cmd_put_returns": [],
        "sdc_entries": [],
        "sdc_returns": [],
        "lock_release_entries": [],
        "lock_release_returns": [],
        "work_submit_entries": [],
        "work_submit_returns": [],
        "snapshot_schedule_markers": [],
        "snapshot_markers": [],
        "scheduler_unlock_arm_markers": [],
        "scheduler_unlock_entries": [],
        "scheduler_unlock_returns": [],
        "scheduler_unlock_state_markers": [],
        "scheduler_yield_arm_markers": [],
        "scheduler_yield_entries": [],
        "scheduler_yield_returns": [],
        "scheduler_yield_state_markers": [],
        "msg_get_entries": [],
        "msg_get_returns": [],
        "sdc_completions": [],
        "receive_disposition_arm_markers": [],
        "receive_disposition_fetch_entries": [],
        "receive_disposition_fetch_returns": [],
        "receive_disposition_allocations": [],
        "iso_rx_lifetime_arm_markers": [],
        "iso_rx_lifetime_snapshots": [],
        "iso_rx_lifetime_disposition_snapshots": [],
        "iso_rx_lifetime_first_free_after_unavailable": [],
        "parser_errors": [],
        "validation_errors": [],
    }

    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    elif not isinstance(raw, (bytes, bytearray, memoryview)):
        message = "receiver SDC trace evidence is not bytes"
        payload["parser_errors"].append(message)
        payload["validation_errors"].append(message)
        return payload
    raw = bytes(raw)

    try:
        raw_lines = tuple(iter_raw_utf8_lines(raw))
    except UnicodeDecodeError as exc:
        message = "receiver SDC trace evidence invalid UTF-8: %s" % exc
        payload["parser_errors"].append(message)
        payload["validation_errors"].append(message)
        raw_lines = ()

    arm_seen = False
    post_arm_entries = 0
    post_arm_returns = 0
    post_arm_completions = 0
    post_arm_cmd_put_entries = 0
    post_arm_cmd_put_returns = 0
    post_arm_lock_release_entries = 0
    post_arm_lock_release_returns = 0
    post_arm_work_submit_entries = 0
    post_arm_work_submit_returns = 0
    post_arm_msg_get_entries = 0
    post_arm_msg_get_returns = 0

    for start_offset, end_offset, raw_line, _complete in raw_lines:
        line = strip_vt100(raw_line).strip()
        classification = "after_arm" if arm_seen else "before_arm"

        lifetime_arm_state, lifetime_capacity = _sdc_hci_iso_rx_lifetime_arm_state(line)
        if lifetime_arm_state is not None:
            if lifetime_arm_state is False:
                payload["parser_errors"].append(
                    "malformed ISO RX lifetime arm marker: %s" % line
                )
            else:
                payload["iso_rx_lifetime_arm_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        capacity=lifetime_capacity,
                    )
                )

        lifetime_snapshot_state, lifetime_snapshot_fields = (
            _sdc_hci_iso_rx_lifetime_snapshot_state(line)
        )
        if lifetime_snapshot_state is not None:
            if lifetime_snapshot_state is False or lifetime_snapshot_fields is None:
                payload["parser_errors"].append(
                    "malformed ISO RX lifetime snapshot marker: %s" % line
                )
            else:
                payload["iso_rx_lifetime_snapshots"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        **lifetime_snapshot_fields,
                    )
                )

        lifetime_disposition_state, lifetime_disposition_fields = (
            _sdc_hci_iso_rx_lifetime_disposition_snapshot_state(line)
        )
        if lifetime_disposition_state is not None:
            if (
                lifetime_disposition_state is False
                or lifetime_disposition_fields is None
            ):
                payload["parser_errors"].append(
                    "malformed ISO RX lifetime disposition snapshot marker: %s" % line
                )
            else:
                payload["iso_rx_lifetime_disposition_snapshots"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        **lifetime_disposition_fields,
                    )
                )

        lifetime_first_free_state, lifetime_first_free_fields = (
            _sdc_hci_iso_rx_lifetime_first_free_state(line)
        )
        if lifetime_first_free_state is not None:
            if lifetime_first_free_state is False or lifetime_first_free_fields is None:
                payload["parser_errors"].append(
                    "malformed ISO RX lifetime first-free marker: %s" % line
                )
            else:
                payload["iso_rx_lifetime_first_free_after_unavailable"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        **lifetime_first_free_fields,
                    )
                )

        arm_state = _sdc_hci_marker_state(line, SDC_HCI_REMOVE_ISO_PATH_TRACE_ARM_TEXT)
        if arm_state is not None:
            if arm_state is False:
                payload["parser_errors"].append("malformed arm marker: %s" % line)
            else:
                payload["arm_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                arm_seen = True

        receive_arm_state, queue_is_mpsl = _sdc_hci_receive_disposition_arm_state(line)
        if receive_arm_state is not None:
            if receive_arm_state is False:
                payload["parser_errors"].append(
                    "malformed receive disposition arm marker: %s" % line
                )
            else:
                payload["receive_disposition_arm_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        queue_is_mpsl=queue_is_mpsl,
                    )
                )

        receive_fetch_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_ENTRY_TEXT
        )
        if receive_fetch_entry_state is not None:
            if receive_fetch_entry_state is False:
                payload["parser_errors"].append(
                    "malformed receive disposition fetch entry marker: %s" % line
                )
            else:
                payload["receive_disposition_fetch_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )

        if (
            SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_RETURN_PREFIX
            in line
        ):
            fetch_return_state = False
            fetch_return_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                fetch_return_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_RETURN.search(
                    candidate
                )
                fetch_return_state = (
                    fetch_return_match is not None
                    and candidate.count(
                        SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_FETCH_RETURN_PREFIX
                    )
                    == 1
                    and fetch_return_match.end() == len(candidate)
                )
            if not fetch_return_state or fetch_return_match is None:
                payload["parser_errors"].append(
                    "malformed receive disposition fetch return marker: %s" % line
                )
            else:
                msg_type = fetch_return_match.group(2)
                payload["receive_disposition_fetch_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=int(fetch_return_match.group(1), 10),
                        msg_type=None if msg_type == "na" else int(msg_type, 10),
                    )
                )

        allocation_state, allocation_kind, allocation_fields = (
            _sdc_hci_receive_disposition_allocation_state(line)
        )
        if allocation_state is not None:
            if (
                allocation_state is False
                or allocation_kind is None
                or allocation_fields is None
            ):
                payload["parser_errors"].append(
                    "malformed receive disposition allocation marker: %s" % line
                )
            else:
                payload["receive_disposition_allocations"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        kind=allocation_kind,
                        **allocation_fields,
                    )
                )

        cmd_put_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_ENTRY_TEXT
        )
        if cmd_put_entry_state is not None:
            if cmd_put_entry_state is False:
                payload["parser_errors"].append(
                    "malformed cmd_put entry marker: %s" % line
                )
            else:
                payload["cmd_put_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if arm_seen:
                    post_arm_cmd_put_entries += 1

        entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_ENTRY_TEXT
        )
        if entry_state is not None:
            if entry_state is False:
                payload["parser_errors"].append("malformed entry marker: %s" % line)
            else:
                payload["sdc_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if arm_seen:
                    if post_arm_entries >= post_arm_cmd_put_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm SDC entry before cmd_put entry"
                        )
                    post_arm_entries += 1

        if SDC_HCI_REMOVE_ISO_PATH_TRACE_RETURN_PREFIX in line:
            return_state = False
            return_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                return_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_RETURN.search(candidate)
                return_state = (
                    return_match is not None
                    and candidate.count(SDC_HCI_REMOVE_ISO_PATH_TRACE_RETURN_PREFIX)
                    == 1
                    and return_match.end() == len(candidate)
                )
            if not return_state or return_match is None:
                payload["parser_errors"].append("malformed return marker: %s" % line)
            else:
                status = int(return_match.group(1), 16)
                payload["sdc_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=status,
                    )
                )
                if arm_seen:
                    if post_arm_returns >= post_arm_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm SDC return before entry"
                        )
                    if status != 0:
                        payload["validation_errors"].append(
                            "nonzero post-arm SDC return status: 0x%02x" % status
                        )
                    post_arm_returns += 1

        if SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_RETURN_PREFIX in line:
            return_state = False
            return_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                return_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_RETURN.search(
                    candidate
                )
                return_state = (
                    return_match is not None
                    and candidate.count(
                        SDC_HCI_REMOVE_ISO_PATH_TRACE_CMD_PUT_RETURN_PREFIX
                    )
                    == 1
                    and return_match.end() == len(candidate)
                )
            if not return_state or return_match is None:
                payload["parser_errors"].append(
                    "malformed cmd_put return marker: %s" % line
                )
            else:
                status = int(return_match.group(1), 10)
                payload["cmd_put_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=status,
                    )
                )
                if arm_seen:
                    if post_arm_cmd_put_returns >= post_arm_cmd_put_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm cmd_put return before entry"
                        )
                    if post_arm_cmd_put_returns >= post_arm_returns:
                        payload["validation_errors"].append(
                            "out-of-order post-arm cmd_put return before SDC return"
                        )
                    if status != 0:
                        payload["validation_errors"].append(
                            "nonzero post-arm cmd_put return status: %d" % status
                        )
                    post_arm_cmd_put_returns += 1

        lock_release_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_LOCK_RELEASE_ENTRY_TEXT
        )
        if lock_release_entry_state is not None:
            if lock_release_entry_state is False:
                payload["parser_errors"].append(
                    "malformed lock_release entry marker: %s" % line
                )
            else:
                payload["lock_release_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if arm_seen:
                    if post_arm_lock_release_entries >= post_arm_cmd_put_returns:
                        payload["validation_errors"].append(
                            "out-of-order post-arm lock_release entry before cmd_put return"
                        )
                    post_arm_lock_release_entries += 1

        lock_release_return_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_LOCK_RELEASE_RETURN_TEXT
        )
        if lock_release_return_state is not None:
            if lock_release_return_state is False:
                payload["parser_errors"].append(
                    "malformed lock_release return marker: %s" % line
                )
            else:
                payload["lock_release_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if arm_seen:
                    if post_arm_lock_release_returns >= post_arm_lock_release_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm lock_release return before entry"
                        )
                    post_arm_lock_release_returns += 1

        work_submit_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_ENTRY_TEXT
        )
        if work_submit_entry_state is not None:
            if work_submit_entry_state is False:
                payload["parser_errors"].append(
                    "malformed work_submit entry marker: %s" % line
                )
            else:
                payload["work_submit_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if arm_seen:
                    if post_arm_work_submit_entries >= post_arm_lock_release_returns:
                        payload["validation_errors"].append(
                            "out-of-order post-arm work_submit entry before lock_release return"
                        )
                    post_arm_work_submit_entries += 1

        if SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_RETURN_PREFIX in line:
            return_state = False
            return_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                return_match = (
                    RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_RETURN.search(
                        candidate
                    )
                )
                return_state = (
                    return_match is not None
                    and candidate.count(
                        SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_SUBMIT_RETURN_PREFIX
                    )
                    == 1
                    and return_match.end() == len(candidate)
                )
            if not return_state or return_match is None:
                payload["parser_errors"].append(
                    "malformed work_submit return marker: %s" % line
                )
            else:
                status = int(return_match.group(1), 10)
                payload["work_submit_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=status,
                    )
                )
                if arm_seen:
                    if post_arm_work_submit_returns >= post_arm_work_submit_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm work_submit return before entry"
                        )
                    if status not in (0, 1, 2):
                        payload["validation_errors"].append(
                            "invalid post-arm k_work_submit_to_queue return status: %d"
                            % status
                        )
                    post_arm_work_submit_returns += 1

        if SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_TEXT in line:
            schedule_state = False
            schedule_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                schedule_match = (
                    RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED.search(
                        candidate
                    )
                )
                schedule_state = (
                    schedule_match is not None
                    and candidate.count(
                        SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_PREFIX
                    )
                    == 1
                    and schedule_match.end() == len(candidate)
                )
            if not schedule_state or schedule_match is None:
                payload["parser_errors"].append(
                    "malformed work state snapshot schedule marker: %s" % line
                )
            else:
                payload["snapshot_schedule_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=int(schedule_match.group(1), 10),
                    )
                )

        if (
            SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_TEXT in line
            and SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_SCHEDULED_TEXT not in line
        ):
            snapshot_state = False
            snapshot_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                snapshot_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT.search(
                    candidate
                )
                snapshot_state = (
                    snapshot_match is not None
                    and candidate.count(SDC_HCI_REMOVE_ISO_PATH_TRACE_SNAPSHOT_PREFIX)
                    == 1
                    and snapshot_match.end() == len(candidate)
                )
            if not snapshot_state or snapshot_match is None:
                payload["parser_errors"].append(
                    "malformed work state snapshot marker: %s" % line
                )
            else:
                payload["snapshot_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        busy=int(snapshot_match.group(1), 16),
                        mpsl_state=snapshot_match.group(2),
                    )
                )

        scheduler_unlock_arm_state, queue_is_mpsl = _sdc_hci_scheduler_unlock_arm_state(
            line
        )
        if scheduler_unlock_arm_state is not None:
            if scheduler_unlock_arm_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler unlock arm marker: %s" % line
                )
            else:
                payload["scheduler_unlock_arm_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        **(
                            {"queue_is_mpsl": queue_is_mpsl}
                            if queue_is_mpsl is not None
                            else {}
                        ),
                    )
                )
                if arm_seen and post_arm_work_submit_returns == 0:
                    payload["validation_errors"].append(
                        "scheduler unlock arm marker before work_submit return"
                    )

        scheduler_unlock_state, scheduler_state_fields = (
            _sdc_hci_scheduler_unlock_state(line)
        )
        if scheduler_unlock_state is not None:
            if scheduler_unlock_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler unlock state marker: %s" % line
                )
            else:
                state_record = _sdc_hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification,
                )
                if scheduler_state_fields is not None:
                    state_record.update(scheduler_state_fields)
                payload["scheduler_unlock_state_markers"].append(state_record)

        scheduler_unlock_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_ENTRY_TEXT
        )
        if scheduler_unlock_entry_state is not None:
            if scheduler_unlock_entry_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler unlock entry marker: %s" % line
                )
            else:
                payload["scheduler_unlock_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if not payload["scheduler_unlock_arm_markers"]:
                    payload["validation_errors"].append(
                        "scheduler unlock entry before scheduler unlock arm"
                    )

        scheduler_unlock_return_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK_RETURN_TEXT
        )
        if scheduler_unlock_return_state is not None:
            if scheduler_unlock_return_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler unlock return marker: %s" % line
                )
            else:
                payload["scheduler_unlock_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if not payload["scheduler_unlock_entries"]:
                    payload["validation_errors"].append(
                        "scheduler unlock return without scheduler unlock entry"
                    )

        scheduler_yield_arm_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_ARM_TEXT
        )
        if scheduler_yield_arm_state is not None:
            if scheduler_yield_arm_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler yield arm marker: %s" % line
                )
            else:
                payload["scheduler_yield_arm_markers"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )

        scheduler_yield_state, scheduler_yield_state_fields = (
            _sdc_hci_scheduler_yield_state(line)
        )
        if scheduler_yield_state is not None:
            if scheduler_yield_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler yield state marker: %s" % line
                )
            else:
                state_record = _sdc_hci_remove_iso_path_trace_record(
                    start_offset,
                    end_offset,
                    line,
                    classification,
                )
                if scheduler_yield_state_fields is not None:
                    state_record.update(scheduler_yield_state_fields)
                payload["scheduler_yield_state_markers"].append(state_record)

        scheduler_yield_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_ENTRY_TEXT
        )
        if scheduler_yield_entry_state is not None:
            if scheduler_yield_entry_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler yield entry marker: %s" % line
                )
            else:
                payload["scheduler_yield_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )

        scheduler_yield_return_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_YIELD_RETURN_TEXT
        )
        if scheduler_yield_return_state is not None:
            if scheduler_yield_return_state is False:
                payload["parser_errors"].append(
                    "malformed scheduler yield return marker: %s" % line
                )
            else:
                payload["scheduler_yield_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )

        msg_get_entry_state = _sdc_hci_marker_state(
            line, SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_ENTRY_TEXT
        )
        if msg_get_entry_state is not None:
            if msg_get_entry_state is False:
                payload["parser_errors"].append(
                    "malformed msg_get entry marker: %s" % line
                )
            else:
                payload["msg_get_entries"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                    )
                )
                if arm_seen:
                    if post_arm_msg_get_entries >= post_arm_work_submit_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm msg_get entry before work_submit entry"
                        )
                    post_arm_msg_get_entries += 1

        if SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_RETURN_PREFIX in line:
            return_state = False
            return_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                return_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_RETURN.search(
                    candidate
                )
                return_state = (
                    return_match is not None
                    and candidate.count(
                        SDC_HCI_REMOVE_ISO_PATH_TRACE_MSG_GET_RETURN_PREFIX
                    )
                    == 1
                    and return_match.end() == len(candidate)
                )
            if not return_state or return_match is None:
                payload["parser_errors"].append(
                    "malformed msg_get return marker: %s" % line
                )
            else:
                status = int(return_match.group(1), 10)
                payload["msg_get_returns"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=status,
                    )
                )
                if arm_seen:
                    if post_arm_msg_get_returns >= post_arm_msg_get_entries:
                        payload["validation_errors"].append(
                            "out-of-order post-arm msg_get return before entry"
                        )
                    post_arm_msg_get_returns += 1

        if SDC_HCI_REMOVE_ISO_PATH_TRACE_COMPLETION_PREFIX in line:
            completion_state = False
            completion_match = None
            candidate = _sdc_hci_marker_candidate(line)
            if candidate is not None:
                completion_match = RE_SDC_HCI_REMOVE_ISO_PATH_TRACE_COMPLETION.search(
                    candidate
                )
                completion_state = (
                    completion_match is not None
                    and candidate.count(SDC_HCI_REMOVE_ISO_PATH_TRACE_COMPLETION_PREFIX)
                    == 1
                    and completion_match.end() == len(candidate)
                )
            if not completion_state or completion_match is None:
                payload["parser_errors"].append(
                    "malformed completion marker: %s" % line
                )
            else:
                status = int(completion_match.group(1), 16)
                payload["sdc_completions"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification,
                        status=status,
                    )
                )
                if arm_seen:
                    if post_arm_completions >= post_arm_cmd_put_returns:
                        payload["validation_errors"].append(
                            "out-of-order post-arm SDC completion before cmd_put return"
                        )
                    if post_arm_completions >= post_arm_returns:
                        payload["validation_errors"].append(
                            "out-of-order post-arm SDC completion before return"
                        )
                    if post_arm_completions >= post_arm_msg_get_returns:
                        payload["validation_errors"].append(
                            "out-of-order post-arm SDC completion before msg_get return"
                        )
                    if status != 0:
                        payload["validation_errors"].append(
                            "nonzero post-arm SDC completion status: 0x%02x" % status
                        )
                    post_arm_completions += 1

        if "messages dropped" in line:
            dropped_line = line
            if dropped_line.startswith(RECEIVER_PROMPT):
                dropped_line = dropped_line[len(RECEIVER_PROMPT) :]
            match = RE_HCI_REMOVE_ISO_PATH_TRACE_DROPPED.fullmatch(dropped_line)
            if match is None:
                payload["parser_errors"].append(
                    "malformed dropped-message record: %s" % line
                )
            else:
                payload["dropped_messages"].append(
                    _sdc_hci_remove_iso_path_trace_record(
                        start_offset,
                        end_offset,
                        line,
                        classification=classification,
                        count=int(match.group("count")),
                    )
                )

    validation_errors = payload["validation_errors"]
    validation_errors.extend(payload["parser_errors"])
    arm_count = len(payload["arm_markers"])
    if arm_count == 0:
        validation_errors.append("missing arm marker")
    elif arm_count > 1:
        validation_errors.append("duplicate arm marker")
        validation_errors.append("duplicate arm marker: found %d" % arm_count)

    for record in payload["dropped_messages"]:
        if record["classification"] == "after_arm":
            validation_errors.append("dropped messages after arm: %d" % record["count"])

    post_arm_entry_records = [
        record
        for record in payload["sdc_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_cmd_put_entry_records = [
        record
        for record in payload["cmd_put_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_cmd_put_return_records = [
        record
        for record in payload["cmd_put_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_lock_release_entry_records = [
        record
        for record in payload["lock_release_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_lock_release_return_records = [
        record
        for record in payload["lock_release_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_work_submit_entry_records = [
        record
        for record in payload["work_submit_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_work_submit_return_records = [
        record
        for record in payload["work_submit_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_snapshot_schedule_records = [
        record
        for record in payload["snapshot_schedule_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_snapshot_records = [
        record
        for record in payload["snapshot_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_unlock_arm_records = [
        record
        for record in payload["scheduler_unlock_arm_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_unlock_entry_records = [
        record
        for record in payload["scheduler_unlock_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_unlock_return_records = [
        record
        for record in payload["scheduler_unlock_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_unlock_state_records = [
        record
        for record in payload["scheduler_unlock_state_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_yield_arm_records = [
        record
        for record in payload["scheduler_yield_arm_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_yield_entry_records = [
        record
        for record in payload["scheduler_yield_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_yield_return_records = [
        record
        for record in payload["scheduler_yield_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_scheduler_yield_state_records = [
        record
        for record in payload["scheduler_yield_state_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_msg_get_entry_records = [
        record
        for record in payload["msg_get_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_msg_get_return_records = [
        record
        for record in payload["msg_get_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_return_records = [
        record
        for record in payload["sdc_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_completion_records = [
        record
        for record in payload["sdc_completions"]
        if record["classification"] == "after_arm"
    ]
    post_arm_entry_count = len(post_arm_entry_records)
    post_arm_cmd_put_entry_count = len(post_arm_cmd_put_entry_records)
    post_arm_cmd_put_return_count = len(post_arm_cmd_put_return_records)
    post_arm_lock_release_entry_count = len(post_arm_lock_release_entry_records)
    post_arm_lock_release_return_count = len(post_arm_lock_release_return_records)
    post_arm_work_submit_entry_count = len(post_arm_work_submit_entry_records)
    post_arm_work_submit_return_count = len(post_arm_work_submit_return_records)
    post_arm_snapshot_schedule_count = len(post_arm_snapshot_schedule_records)
    post_arm_snapshot_count = len(post_arm_snapshot_records)
    post_arm_scheduler_unlock_arm_count = len(post_arm_scheduler_unlock_arm_records)
    post_arm_scheduler_unlock_entry_count = len(post_arm_scheduler_unlock_entry_records)
    post_arm_scheduler_unlock_return_count = len(
        post_arm_scheduler_unlock_return_records
    )
    post_arm_scheduler_unlock_state_count = len(post_arm_scheduler_unlock_state_records)
    post_arm_scheduler_yield_arm_count = len(post_arm_scheduler_yield_arm_records)
    post_arm_scheduler_yield_entry_count = len(post_arm_scheduler_yield_entry_records)
    post_arm_scheduler_yield_return_count = len(post_arm_scheduler_yield_return_records)
    post_arm_scheduler_yield_state_count = len(post_arm_scheduler_yield_state_records)
    post_arm_msg_get_entry_count = len(post_arm_msg_get_entry_records)
    post_arm_msg_get_return_count = len(post_arm_msg_get_return_records)
    post_arm_return_count = len(post_arm_return_records)
    post_arm_completion_count = len(post_arm_completion_records)

    post_arm_receive_arm_records = [
        record
        for record in payload["receive_disposition_arm_markers"]
        if record["classification"] == "after_arm"
    ]
    post_arm_receive_fetch_entry_records = [
        record
        for record in payload["receive_disposition_fetch_entries"]
        if record["classification"] == "after_arm"
    ]
    post_arm_receive_fetch_return_records = [
        record
        for record in payload["receive_disposition_fetch_returns"]
        if record["classification"] == "after_arm"
    ]
    post_arm_receive_allocation_records = [
        record
        for record in payload["receive_disposition_allocations"]
        if record["classification"] == "after_arm"
    ]
    receive_arm_count = len(payload["receive_disposition_arm_markers"])
    receive_arm_offset = (
        post_arm_receive_arm_records[0]["start_offset"]
        if post_arm_receive_arm_records
        else None
    )
    receive_scoped_fetch_entry_records = [
        record
        for record in post_arm_receive_fetch_entry_records
        if receive_arm_offset is not None
        and record["start_offset"] > receive_arm_offset
    ]
    receive_scoped_fetch_return_records = [
        record
        for record in post_arm_receive_fetch_return_records
        if receive_arm_offset is not None
        and record["start_offset"] > receive_arm_offset
    ]
    receive_scoped_allocation_records = [
        record
        for record in post_arm_receive_allocation_records
        if receive_arm_offset is not None
        and record["start_offset"] > receive_arm_offset
    ]
    receive_fetch_entry_count = len(receive_scoped_fetch_entry_records)
    receive_fetch_return_count = len(receive_scoped_fetch_return_records)
    receive_allocation_count = len(receive_scoped_allocation_records)
    receive_disposition_outcome = None
    receive_disposition_suppress_msg_get = False
    receive_disposition_suppress_completion = False

    if receive_arm_count == 0:
        if (
            payload["receive_disposition_fetch_entries"]
            or payload["receive_disposition_fetch_returns"]
            or payload["receive_disposition_allocations"]
        ):
            validation_errors.append(
                "receive disposition record without receive disposition arm marker"
            )
    else:
        if receive_arm_count > 1:
            validation_errors.append("duplicate receive disposition arm marker")
        for record in payload["receive_disposition_arm_markers"]:
            if record["classification"] == "before_arm":
                validation_errors.append(
                    "receive disposition arm marker before trace arm"
                )
            if record["queue_is_mpsl"] != 1:
                validation_errors.append(
                    "receive disposition arm queue_is_mpsl must be 1, found %d"
                    % record["queue_is_mpsl"]
                )

        for records, label in (
            (
                payload["receive_disposition_fetch_entries"],
                "receive disposition fetch entry",
            ),
            (
                payload["receive_disposition_fetch_returns"],
                "receive disposition fetch return",
            ),
            (
                payload["receive_disposition_allocations"],
                "receive disposition allocation",
            ),
        ):
            if len(records) > 1:
                validation_errors.append("duplicate %s marker" % label)
            for record in records:
                if record["classification"] == "before_arm":
                    validation_errors.append("%s marker before trace arm" % label)

        if receive_arm_offset is not None:
            for records, label in (
                (
                    post_arm_receive_fetch_entry_records,
                    "receive disposition fetch entry",
                ),
                (
                    post_arm_receive_fetch_return_records,
                    "receive disposition fetch return",
                ),
                (
                    post_arm_receive_allocation_records,
                    "receive disposition allocation",
                ),
            ):
                for record in records:
                    if record["start_offset"] <= receive_arm_offset:
                        validation_errors.append(
                            "%s marker before receive disposition arm" % label
                        )

        if receive_fetch_entry_count != receive_fetch_return_count:
            validation_errors.append(
                "receive disposition fetch entry/return count mismatch: entries=%d returns=%d"
                % (receive_fetch_entry_count, receive_fetch_return_count)
            )
        if receive_fetch_entry_count and receive_fetch_entry_count != 1:
            validation_errors.append(
                "expected exactly one post-arm receive disposition fetch entry, found %d"
                % receive_fetch_entry_count
            )
        if receive_fetch_return_count and receive_fetch_return_count != 1:
            validation_errors.append(
                "expected exactly one post-arm receive disposition fetch return, found %d"
                % receive_fetch_return_count
            )
        fetch_return = (
            receive_scoped_fetch_return_records[0]
            if receive_fetch_return_count == 1
            else None
        )
        if fetch_return is not None and receive_fetch_entry_count == 1:
            if (
                receive_scoped_fetch_entry_records[0]["start_offset"]
                >= fetch_return["start_offset"]
            ):
                validation_errors.append(
                    "out-of-order receive disposition fetch return before entry"
                )
        if fetch_return is not None:
            if fetch_return["status"] != 0:
                if receive_allocation_count:
                    validation_errors.append(
                        "fetch-error receive disposition must not have allocation"
                    )
                elif post_arm_completion_count:
                    validation_errors.append(
                        "fetch-error receive disposition must not have completion"
                    )
                else:
                    receive_disposition_outcome = "fetch_error"
                    receive_disposition_suppress_completion = True
            elif fetch_return["msg_type"] is None:
                validation_errors.append(
                    "successful receive disposition fetch missing msg_type"
                )
            elif (
                _sdc_hci_receive_disposition_expected_allocation(
                    fetch_return["msg_type"]
                )
                is None
            ):
                validation_errors.append(
                    "unknown successful receive disposition msg_type: %d"
                    % fetch_return["msg_type"]
                )

        if (
            fetch_return is None or fetch_return["status"] == 0
        ) and receive_allocation_count != 1:
            validation_errors.append(
                "expected exactly one post-arm receive disposition allocation, found %d"
                % receive_allocation_count
            )

        for record in receive_scoped_allocation_records:
            if (record["target_busy"] & 0x1) == 0:
                validation_errors.append(
                    "receive disposition allocation target_busy lacks K_WORK_RUNNING"
                )

        if (
            fetch_return is not None
            and fetch_return["status"] == 0
            and receive_allocation_count
        ):
            if (
                fetch_return["start_offset"]
                >= receive_scoped_allocation_records[0]["start_offset"]
            ):
                validation_errors.append(
                    "out-of-order receive disposition allocation before fetch return"
                )
            elif not _sdc_hci_receive_disposition_allocation_matches(
                fetch_return["msg_type"], receive_scoped_allocation_records[0]
            ):
                validation_errors.append(
                    "receive disposition fetch/allocation type mismatch: msg_type=%d kind=%s"
                    % (
                        fetch_return["msg_type"],
                        receive_scoped_allocation_records[0]["kind"],
                    )
                )

        retained_iso_buffer_unavailable = (
            receive_fetch_entry_count == 0
            and receive_fetch_return_count == 0
            and receive_allocation_count == 1
            and receive_scoped_allocation_records[0]["kind"] == "rx"
            and receive_scoped_allocation_records[0]["type"] == 32
            and not receive_scoped_allocation_records[0]["buffer_available"]
            and (receive_scoped_allocation_records[0]["target_busy"] & 0x1) != 0
            and post_arm_msg_get_entry_count == 0
            and post_arm_msg_get_return_count == 0
            and post_arm_completion_count == 0
        )
        retained_iso_buffer_unavailable_candidate = (
            receive_fetch_entry_count == 0
            and receive_fetch_return_count == 0
            and receive_allocation_count == 1
            and receive_scoped_allocation_records[0]["kind"] == "rx"
            and receive_scoped_allocation_records[0]["type"] == 32
            and not receive_scoped_allocation_records[0]["buffer_available"]
            and (receive_scoped_allocation_records[0]["target_busy"] & 0x1) != 0
        )
        if (
            retained_iso_buffer_unavailable_candidate
            and not retained_iso_buffer_unavailable
        ):
            if post_arm_msg_get_entry_count or post_arm_msg_get_return_count:
                validation_errors.append(
                    "retained ISO buffer unavailable must not have hci_internal_msg_get"
                )
            if post_arm_completion_count:
                validation_errors.append(
                    "retained ISO buffer unavailable must not have completion"
                )
        elif retained_iso_buffer_unavailable:
            receive_disposition_outcome = "retained_iso_buffer_unavailable"
            receive_disposition_suppress_msg_get = True
            receive_disposition_suppress_completion = True

        fetched_buffer_unavailable = (
            fetch_return is not None
            and fetch_return["status"] == 0
            and _sdc_hci_receive_disposition_expected_allocation(
                fetch_return["msg_type"]
            )
            is not None
            and receive_allocation_count == 1
            and not receive_scoped_allocation_records[0]["buffer_available"]
            and (receive_scoped_allocation_records[0]["target_busy"] & 0x1) != 0
            and _sdc_hci_receive_disposition_allocation_matches(
                fetch_return["msg_type"], receive_scoped_allocation_records[0]
            )
        )
        if fetched_buffer_unavailable:
            if post_arm_completion_count > 1:
                validation_errors.append(
                    "fetched buffer unavailable allows at most one completion marker"
                )
            elif post_arm_completion_count == 1:
                completion_record = post_arm_completion_records[0]
                fetch_return_offset = receive_scoped_fetch_return_records[0][
                    "start_offset"
                ]
                allocation_offset = receive_scoped_allocation_records[0]["start_offset"]
                if completion_record["start_offset"] <= fetch_return_offset:
                    validation_errors.append(
                        "fetched buffer unavailable completion must follow scoped receive fetch return"
                    )
                if completion_record["start_offset"] >= allocation_offset:
                    validation_errors.append(
                        "fetched buffer unavailable completion must precede scoped allocation"
                    )
            receive_disposition_outcome = "fetched_buffer_unavailable"
            receive_disposition_suppress_completion = True

    payload["receive_disposition_outcome"] = receive_disposition_outcome

    lifetime_arm_records = payload["iso_rx_lifetime_arm_markers"]
    lifetime_snapshot_records = payload["iso_rx_lifetime_snapshots"]
    lifetime_disposition_records = payload["iso_rx_lifetime_disposition_snapshots"]
    lifetime_first_free_records = payload[
        "iso_rx_lifetime_first_free_after_unavailable"
    ]
    lifetime_records = lifetime_snapshot_records + lifetime_first_free_records
    if not lifetime_arm_records:
        if lifetime_records:
            validation_errors.append(
                "ISO RX lifetime record without lifetime arm marker"
            )
        if lifetime_disposition_records:
            validation_errors.append(
                "ISO RX lifetime disposition record without lifetime arm marker"
            )
    else:
        if len(lifetime_arm_records) > 1:
            validation_errors.append("duplicate ISO RX lifetime arm marker")
            validation_errors.append(
                "duplicate ISO RX lifetime arm marker: found %d"
                % len(lifetime_arm_records)
            )

        lifetime_arm = lifetime_arm_records[0]
        lifetime_capacity = lifetime_arm["capacity"]
        if lifetime_capacity <= 0:
            validation_errors.append("ISO RX lifetime arm capacity must be positive")

        for record in lifetime_snapshot_records:
            if record["start_offset"] <= lifetime_arm["start_offset"]:
                validation_errors.append(
                    "ISO RX lifetime snapshot marker before lifetime arm"
                )
            if record["capacity"] <= 0 or record["capacity"] != lifetime_capacity:
                validation_errors.append(
                    "ISO RX lifetime snapshot capacity does not match arm capacity"
                )
            if record["outstanding"] > lifetime_capacity:
                validation_errors.append(
                    "ISO RX lifetime snapshot outstanding exceeds arm capacity"
                )
            if record["high_water"] > lifetime_capacity:
                validation_errors.append(
                    "ISO RX lifetime snapshot high_water exceeds arm capacity"
                )

        for record in lifetime_disposition_records:
            if record["start_offset"] <= lifetime_arm["start_offset"]:
                validation_errors.append(
                    "ISO RX lifetime disposition snapshot marker before lifetime arm"
                )
            if record["reason"] not in ("disable", "unavailable"):
                validation_errors.append(
                    "ISO RX lifetime disposition snapshot has invalid reason=%s"
                    % record["reason"]
                )
            for field in (
                "undispatched",
                "host_dispatched",
                "tx_notify_flush_entered",
                "tx_notify_flush_semaphore_entered",
                "tx_notify_flush_semaphore_pend_entered",
                "tx_notify_flush_semaphore_pend_thread_marked_pending",
                "tx_notify_flush_semaphore_give_entered",
                "tx_notify_flush_semaphore_pend_returned",
                "tx_notify_flush_semaphore_returned",
                "tx_notify_flush_returned",
                "host_returned",
                "app_callback_seen",
                "unclassified",
            ):
                if record[field] is not None and record[field] > lifetime_capacity:
                    validation_errors.append(
                        "ISO RX lifetime disposition %s exceeds arm capacity: %d"
                        % (field, record[field])
                    )
            if (
                sum(
                    record[field]
                    for field in (
                        "undispatched",
                        "host_dispatched",
                        "tx_notify_flush_entered",
                        "tx_notify_flush_semaphore_entered",
                        "tx_notify_flush_semaphore_pend_entered",
                        "tx_notify_flush_semaphore_pend_thread_marked_pending",
                        "tx_notify_flush_semaphore_give_entered",
                        "tx_notify_flush_semaphore_pend_returned",
                        "tx_notify_flush_semaphore_returned",
                        "tx_notify_flush_returned",
                        "host_returned",
                        "app_callback_seen",
                        "unclassified",
                    )
                    if record[field] is not None
                )
                > lifetime_capacity
            ):
                validation_errors.append(
                    "ISO RX lifetime disposition count sum exceeds arm capacity"
                )

            matching_snapshots = [
                snapshot
                for snapshot in lifetime_snapshot_records
                if snapshot["reason"] == record["reason"]
            ]
            if not matching_snapshots:
                validation_errors.append(
                    "ISO RX lifetime disposition snapshot reason=%s without matching "
                    "lifetime snapshot" % record["reason"]
                )
            elif not any(
                snapshot["start_offset"] < record["start_offset"]
                for snapshot in matching_snapshots
            ):
                validation_errors.append(
                    "ISO RX lifetime disposition snapshot reason=%s must follow "
                    "matching lifetime snapshot" % record["reason"]
                )

        for reason in ("disable", "unavailable"):
            reason_count = sum(
                record["reason"] == reason for record in lifetime_disposition_records
            )
            if reason_count > 1:
                validation_errors.append(
                    "duplicate ISO RX lifetime disposition snapshot reason=%s" % reason
                )

        for reason in ("stream_start", "disable", "unavailable"):
            reason_count = sum(
                record["reason"] == reason for record in lifetime_snapshot_records
            )
            if reason_count > 1:
                validation_errors.append(
                    "duplicate ISO RX lifetime snapshot reason=%s" % reason
                )

        for record in lifetime_first_free_records:
            if record["start_offset"] <= lifetime_arm["start_offset"]:
                validation_errors.append(
                    "ISO RX lifetime first-free marker before lifetime arm"
                )
            if record["outstanding_before"] == 0:
                validation_errors.append(
                    "ISO RX lifetime first-free outstanding_before must be nonzero"
                )
            elif record["outstanding_before"] > lifetime_capacity:
                validation_errors.append(
                    "ISO RX lifetime first-free outstanding_before exceeds arm capacity"
                )

        if len(lifetime_first_free_records) > 1:
            validation_errors.append("duplicate ISO RX lifetime first-free marker")

        unavailable_records = [
            record
            for record in lifetime_snapshot_records
            if record["reason"] == "unavailable"
        ]
        if len(unavailable_records) > 1:
            validation_errors.append("duplicate ISO RX lifetime unavailable snapshot")
        if lifetime_first_free_records:
            if not unavailable_records:
                validation_errors.append(
                    "ISO RX lifetime first-free marker without unavailable snapshot"
                )
            elif (
                lifetime_first_free_records[0]["start_offset"]
                <= unavailable_records[0]["start_offset"]
            ):
                validation_errors.append(
                    "ISO RX lifetime first-free marker before unavailable snapshot"
                )

        disable_records = [
            record
            for record in lifetime_snapshot_records
            if record["reason"] == "disable"
        ]
        if len(disable_records) > 1:
            validation_errors.append("duplicate ISO RX lifetime disable snapshot")

    def require_trace_precedes(before_name, before_records, after_name, after_records):
        if not before_records or not after_records:
            return
        if max(record["start_offset"] for record in before_records) >= min(
            record["start_offset"] for record in after_records
        ):
            validation_errors.append(
                "out-of-order post-arm trace event: expected %s before %s"
                % (before_name, after_name)
            )

    for before_name, before_records, after_name, after_records in (
        (
            "cmd_put entry",
            post_arm_cmd_put_entry_records,
            "SDC entry",
            post_arm_entry_records,
        ),
        ("SDC entry", post_arm_entry_records, "SDC return", post_arm_return_records),
        (
            "SDC return",
            post_arm_return_records,
            "cmd_put return",
            post_arm_cmd_put_return_records,
        ),
        (
            "cmd_put return",
            post_arm_cmd_put_return_records,
            "lock_release entry",
            post_arm_lock_release_entry_records,
        ),
        (
            "lock_release entry",
            post_arm_lock_release_entry_records,
            "lock_release return",
            post_arm_lock_release_return_records,
        ),
        (
            "lock_release return",
            post_arm_lock_release_return_records,
            "work_submit entry",
            post_arm_work_submit_entry_records,
        ),
        (
            "work_submit entry",
            post_arm_work_submit_entry_records,
            "work_submit return",
            post_arm_work_submit_return_records,
        ),
        (
            "work_submit return",
            post_arm_work_submit_return_records,
            "work state snapshot schedule",
            post_arm_snapshot_schedule_records,
        ),
        (
            "work_submit return",
            post_arm_work_submit_return_records,
            "scheduler unlock arm",
            post_arm_scheduler_unlock_arm_records,
        ),
        (
            "scheduler unlock arm",
            post_arm_scheduler_unlock_arm_records,
            "scheduler unlock entry",
            post_arm_scheduler_unlock_entry_records,
        ),
        (
            "scheduler unlock entry",
            post_arm_scheduler_unlock_entry_records,
            "scheduler unlock return",
            post_arm_scheduler_unlock_return_records,
        ),
        (
            "scheduler unlock entry",
            post_arm_scheduler_unlock_entry_records,
            "scheduler unlock state",
            post_arm_scheduler_unlock_state_records,
        ),
        (
            "scheduler unlock state",
            post_arm_scheduler_unlock_state_records,
            "scheduler unlock return",
            post_arm_scheduler_unlock_return_records,
        ),
        (
            "scheduler unlock return",
            post_arm_scheduler_unlock_return_records,
            "post-unlock yield arm",
            post_arm_scheduler_yield_arm_records,
        ),
        (
            "post-unlock yield arm",
            post_arm_scheduler_yield_arm_records,
            "post-unlock yield entry",
            post_arm_scheduler_yield_entry_records,
        ),
        (
            "post-unlock yield entry",
            post_arm_scheduler_yield_entry_records,
            "post-unlock yield state",
            post_arm_scheduler_yield_state_records,
        ),
        (
            "post-unlock yield state",
            post_arm_scheduler_yield_state_records,
            "post-unlock yield return",
            post_arm_scheduler_yield_return_records,
        ),
        (
            "work state snapshot schedule",
            post_arm_snapshot_schedule_records,
            "work state snapshot",
            post_arm_snapshot_records,
        ),
        (
            "work_submit entry",
            post_arm_work_submit_entry_records,
            "msg_get entry",
            post_arm_msg_get_entry_records,
        ),
        (
            "msg_get entry",
            post_arm_msg_get_entry_records,
            "msg_get return",
            post_arm_msg_get_return_records,
        ),
        (
            "msg_get return",
            post_arm_msg_get_return_records,
            "completion",
            post_arm_completion_records,
        ),
    ):
        require_trace_precedes(before_name, before_records, after_name, after_records)
    if post_arm_cmd_put_entry_count == 0:
        validation_errors.append("missing post-arm cmd_put entry marker")
    if post_arm_cmd_put_entry_count != post_arm_entry_count:
        validation_errors.append(
            "post-arm cmd_put entry/SDC entry count mismatch: cmd_put_entries=%d SDC_entries=%d"
            % (post_arm_cmd_put_entry_count, post_arm_entry_count)
        )
    if post_arm_cmd_put_entry_count != post_arm_cmd_put_return_count:
        validation_errors.append(
            "post-arm cmd_put entry/return count mismatch: entries=%d returns=%d"
            % (post_arm_cmd_put_entry_count, post_arm_cmd_put_return_count)
        )
        if post_arm_cmd_put_entry_count > post_arm_cmd_put_return_count:
            validation_errors.append("missing post-arm cmd_put return")
            validation_errors.append(
                "missing post-arm cmd_put return: expected %d, found %d"
                % (post_arm_cmd_put_entry_count, post_arm_cmd_put_return_count)
            )
        else:
            validation_errors.append(
                "unexpected post-arm cmd_put return: expected %d, found %d"
                % (post_arm_cmd_put_entry_count, post_arm_cmd_put_return_count)
            )
    if post_arm_lock_release_entry_count == 0:
        validation_errors.append("missing post-arm lock_release entry marker")
    if post_arm_cmd_put_return_count != post_arm_lock_release_entry_count:
        validation_errors.append(
            "post-arm cmd_put return/lock_release entry count mismatch: returns=%d entries=%d"
            % (post_arm_cmd_put_return_count, post_arm_lock_release_entry_count)
        )
    if post_arm_lock_release_entry_count != post_arm_lock_release_return_count:
        validation_errors.append(
            "post-arm lock_release entry/return count mismatch: entries=%d returns=%d"
            % (post_arm_lock_release_entry_count, post_arm_lock_release_return_count)
        )
        if post_arm_lock_release_entry_count > post_arm_lock_release_return_count:
            validation_errors.append("missing post-arm lock_release return")
        else:
            validation_errors.append("unexpected post-arm lock_release return")
    if post_arm_work_submit_entry_count == 0:
        validation_errors.append("missing post-arm work_submit entry marker")
    if post_arm_lock_release_return_count != post_arm_work_submit_entry_count:
        validation_errors.append(
            "post-arm lock_release return/work_submit entry count mismatch: returns=%d entries=%d"
            % (post_arm_lock_release_return_count, post_arm_work_submit_entry_count)
        )
    if post_arm_work_submit_entry_count != post_arm_work_submit_return_count:
        validation_errors.append(
            "post-arm work_submit entry/return count mismatch: entries=%d returns=%d"
            % (post_arm_work_submit_entry_count, post_arm_work_submit_return_count)
        )
        if post_arm_work_submit_entry_count > post_arm_work_submit_return_count:
            validation_errors.append("missing post-arm work_submit return")
        else:
            validation_errors.append("unexpected post-arm work_submit return")
    if len(payload["snapshot_schedule_markers"]) > 1:
        validation_errors.append("duplicate work state snapshot schedule marker")
    if len(payload["snapshot_markers"]) > 1:
        validation_errors.append("duplicate work state snapshot marker")
    for record in payload["snapshot_schedule_markers"]:
        if record["classification"] == "before_arm":
            validation_errors.append("work state snapshot schedule marker before arm")
    for record in payload["snapshot_markers"]:
        if record["classification"] == "before_arm":
            validation_errors.append("work state snapshot marker before arm")
    if post_arm_snapshot_schedule_count:
        if not post_arm_work_submit_return_records:
            validation_errors.append(
                "work state snapshot schedule marker without post-arm work_submit return"
            )
        for record in post_arm_snapshot_schedule_records:
            if record["status"] < 0:
                validation_errors.append(
                    "negative post-arm work state snapshot schedule status: %d"
                    % record["status"]
                )
                validation_errors.append(
                    "work state snapshot unavailable after scheduling failure"
                )
        if all(record["status"] >= 0 for record in post_arm_snapshot_schedule_records):
            if post_arm_snapshot_count == 0:
                validation_errors.append(
                    "missing post-arm work state snapshot marker after successful scheduling"
                )
    if post_arm_snapshot_count and not post_arm_snapshot_schedule_count:
        validation_errors.append(
            "work state snapshot marker without post-arm schedule marker"
        )
    if post_arm_snapshot_count and any(
        record["status"] < 0 for record in post_arm_snapshot_schedule_records
    ):
        validation_errors.append(
            "unexpected post-arm work state snapshot after scheduling failure"
        )
    if len(payload["scheduler_unlock_arm_markers"]) > 1:
        validation_errors.append("duplicate scheduler unlock arm marker")
    if len(payload["scheduler_unlock_entries"]) > 1:
        validation_errors.append("duplicate scheduler unlock entry marker")
    if len(payload["scheduler_unlock_returns"]) > 1:
        validation_errors.append("duplicate scheduler unlock return marker")
    if len(payload["scheduler_unlock_state_markers"]) > 1:
        validation_errors.append("duplicate scheduler unlock state marker")
    for record in payload["scheduler_unlock_arm_markers"]:
        if record["classification"] == "before_arm":
            validation_errors.append("scheduler unlock arm marker before trace arm")
    for record in payload["scheduler_unlock_entries"]:
        if record["classification"] == "before_arm":
            validation_errors.append("scheduler unlock entry marker before trace arm")
    for record in payload["scheduler_unlock_returns"]:
        if record["classification"] == "before_arm":
            validation_errors.append("scheduler unlock return marker before trace arm")
    for record in payload["scheduler_unlock_state_markers"]:
        if record["classification"] == "before_arm":
            validation_errors.append("scheduler unlock state marker before trace arm")

    extended_scheduler_unlock_arm_records = [
        record
        for record in payload["scheduler_unlock_arm_markers"]
        if "queue_is_mpsl" in record
    ]
    if (
        payload["scheduler_unlock_state_markers"]
        and not extended_scheduler_unlock_arm_records
    ):
        validation_errors.append(
            "scheduler unlock state marker without extended scheduler unlock arm"
        )
    for record in extended_scheduler_unlock_arm_records:
        if record["queue_is_mpsl"] != 1:
            validation_errors.append(
                "scheduler unlock arm queue_is_mpsl must be 1, found %d"
                % record["queue_is_mpsl"]
            )
    if extended_scheduler_unlock_arm_records:
        extended_arm = extended_scheduler_unlock_arm_records[0]
        for record in payload["scheduler_unlock_state_markers"]:
            if record["start_offset"] <= extended_arm["start_offset"]:
                validation_errors.append(
                    "scheduler unlock state marker before scheduler unlock arm"
                )
        if post_arm_scheduler_unlock_return_count:
            if post_arm_scheduler_unlock_state_count == 0:
                validation_errors.append(
                    "missing post-arm scheduler unlock state marker"
                )
            elif post_arm_scheduler_unlock_state_count == 1:
                state_record = post_arm_scheduler_unlock_state_records[0]
                if (
                    post_arm_scheduler_unlock_entry_records
                    and state_record["start_offset"]
                    <= post_arm_scheduler_unlock_entry_records[0]["start_offset"]
                ):
                    validation_errors.append(
                        "out-of-order scheduler unlock state marker before entry"
                    )
                if (
                    state_record["start_offset"]
                    >= post_arm_scheduler_unlock_return_records[0]["start_offset"]
                ):
                    validation_errors.append(
                        "out-of-order scheduler unlock state marker after return"
                    )
    if payload["scheduler_unlock_arm_markers"]:
        if post_arm_scheduler_unlock_arm_count == 0:
            validation_errors.append("scheduler unlock arm marker before trace arm")
        if post_arm_scheduler_unlock_entry_count == 0:
            validation_errors.append("missing post-arm scheduler unlock entry marker")
        if post_arm_scheduler_unlock_return_count == 0:
            validation_errors.append("missing post-arm scheduler unlock return marker")
    else:
        if payload["scheduler_unlock_entries"]:
            validation_errors.append("scheduler unlock entry without arm marker")
        if payload["scheduler_unlock_returns"]:
            validation_errors.append("scheduler unlock return without arm marker")
    if (
        post_arm_scheduler_unlock_return_count
        and post_arm_scheduler_unlock_entry_count == 0
    ):
        validation_errors.append("scheduler unlock return without post-arm entry")

    for marker_name, records in (
        ("arm", payload["scheduler_yield_arm_markers"]),
        ("entry", payload["scheduler_yield_entries"]),
        ("state", payload["scheduler_yield_state_markers"]),
        ("return", payload["scheduler_yield_returns"]),
    ):
        for record in records:
            if record["classification"] == "before_arm":
                validation_errors.append(
                    "scheduler yield %s marker before trace arm" % marker_name
                )

    scheduler_yield_arm_count = len(payload["scheduler_yield_arm_markers"])
    if scheduler_yield_arm_count > 1:
        validation_errors.append("duplicate scheduler yield arm marker")
    if len(payload["scheduler_yield_entries"]) > 1:
        validation_errors.append("duplicate scheduler yield entry marker")
    if len(payload["scheduler_yield_state_markers"]) > 1:
        validation_errors.append("duplicate scheduler yield state marker")
    if len(payload["scheduler_yield_returns"]) > 1:
        validation_errors.append("duplicate scheduler yield return marker")

    if scheduler_yield_arm_count:
        if post_arm_scheduler_yield_arm_count != 1:
            validation_errors.append(
                "expected exactly one post-arm scheduler yield arm marker, found %d"
                % post_arm_scheduler_yield_arm_count
            )
        for label, count in (
            ("entry", post_arm_scheduler_yield_entry_count),
            ("state", post_arm_scheduler_yield_state_count),
            ("return", post_arm_scheduler_yield_return_count),
        ):
            if count == 0:
                validation_errors.append(
                    "missing post-arm scheduler yield %s marker" % label
                )
            elif count != 1:
                validation_errors.append(
                    "expected exactly one post-arm scheduler yield %s marker, found %d"
                    % (label, count)
                )
    elif any(
        payload[key]
        for key in (
            "scheduler_yield_entries",
            "scheduler_yield_state_markers",
            "scheduler_yield_returns",
        )
    ):
        validation_errors.append("scheduler yield marker without arm marker")
    if not receive_disposition_suppress_msg_get:
        if post_arm_msg_get_entry_count == 0:
            validation_errors.append("missing post-arm msg_get entry marker")
        if post_arm_work_submit_entry_count != post_arm_msg_get_entry_count:
            validation_errors.append(
                "post-arm work_submit entry/msg_get entry count mismatch: work_submit=%d msg_get=%d"
                % (post_arm_work_submit_entry_count, post_arm_msg_get_entry_count)
            )
        if post_arm_msg_get_entry_count != post_arm_msg_get_return_count:
            validation_errors.append(
                "post-arm msg_get entry/return count mismatch: entries=%d returns=%d"
                % (post_arm_msg_get_entry_count, post_arm_msg_get_return_count)
            )
            if post_arm_msg_get_entry_count > post_arm_msg_get_return_count:
                validation_errors.append("missing post-arm msg_get return")
                validation_errors.append(
                    "missing post-arm msg_get return: expected %d, found %d"
                    % (post_arm_msg_get_entry_count, post_arm_msg_get_return_count)
                )
            else:
                validation_errors.append(
                    "unexpected post-arm msg_get return: expected %d, found %d"
                    % (post_arm_msg_get_entry_count, post_arm_msg_get_return_count)
                )
    if post_arm_entry_count == 0:
        validation_errors.append("missing post-arm SDC entry marker")
    if post_arm_entry_count != post_arm_return_count:
        validation_errors.append(
            "post-arm SDC entry/return count mismatch: entries=%d returns=%d"
            % (post_arm_entry_count, post_arm_return_count)
        )
        if post_arm_entry_count > post_arm_return_count:
            validation_errors.append("missing post-arm SDC return")
            validation_errors.append(
                "missing post-arm SDC return: expected %d, found %d"
                % (post_arm_entry_count, post_arm_return_count)
            )
        else:
            validation_errors.append(
                "unexpected post-arm SDC return: expected %d, found %d"
                % (post_arm_entry_count, post_arm_return_count)
            )

    if not receive_disposition_suppress_completion:
        if post_arm_return_count != post_arm_completion_count:
            validation_errors.append(
                "post-arm SDC return/completion count mismatch: returns=%d completions=%d"
                % (post_arm_return_count, post_arm_completion_count)
            )
            if post_arm_return_count > post_arm_completion_count:
                validation_errors.append("missing post-arm SDC completion")
                validation_errors.append(
                    "missing post-arm SDC completion: expected %d, found %d"
                    % (post_arm_return_count, post_arm_completion_count)
                )
            else:
                validation_errors.append(
                    "unexpected post-arm SDC completion: expected %d, found %d"
                    % (post_arm_return_count, post_arm_completion_count)
                )
        if post_arm_msg_get_return_count != post_arm_completion_count:
            validation_errors.append(
                "post-arm msg_get return/completion count mismatch: returns=%d completions=%d"
                % (post_arm_msg_get_return_count, post_arm_completion_count)
            )

    for record in post_arm_return_records:
        if record["status"] != 0:
            # Keep this final validation independent of parser ordering so a
            # record assembled by a future parser change still fails closed.
            message = "nonzero post-arm SDC return status: 0x%02x" % record["status"]
            if message not in validation_errors:
                validation_errors.append(message)

    for record in post_arm_cmd_put_return_records:
        if record["status"] != 0:
            message = "nonzero post-arm cmd_put return status: %d" % record["status"]
            if message not in validation_errors:
                validation_errors.append(message)

    for record in post_arm_work_submit_return_records:
        if record["status"] not in (0, 1, 2):
            message = (
                "invalid post-arm k_work_submit_to_queue return status: %d"
                % record["status"]
            )
            if message not in validation_errors:
                validation_errors.append(message)

    for record in post_arm_msg_get_return_records:
        if record["status"] != 0:
            allowed_fetch_error = (
                receive_disposition_outcome == "fetch_error"
                and len(receive_scoped_fetch_return_records) == 1
                and len(post_arm_msg_get_return_records) == 1
                and record["status"] == receive_scoped_fetch_return_records[0]["status"]
                and record["start_offset"]
                < receive_scoped_fetch_return_records[0]["start_offset"]
            )
            if not allowed_fetch_error:
                message = (
                    "nonzero post-arm msg_get return status: %d" % record["status"]
                )
                if message not in validation_errors:
                    validation_errors.append(message)

    for record in post_arm_completion_records:
        if record["status"] != 0:
            message = (
                "nonzero post-arm SDC completion status: 0x%02x" % record["status"]
            )
            if message not in validation_errors:
                validation_errors.append(message)

    return payload


def parse_iso_link_quality(text):
    """Parse exact ``bt iso quality`` output into ordered stream records."""
    result = {"header_seen": False, "streams": [], "malformed": False}
    for line in strip_vt100(text).splitlines():
        if RE_ISO_LINK_QUALITY_HEADER.fullmatch(line):
            if result["header_seen"]:
                result["malformed"] = True
            result["header_seen"] = True
            continue
        if not result["header_seen"]:
            continue
        if not RE_ISO_LINK_QUALITY_STREAM_CANDIDATE.match(line):
            continue
        match = RE_ISO_LINK_QUALITY_STREAM.fullmatch(line)
        if match is None:
            result["malformed"] = True
            continue
        result["streams"].append(
            {
                "slot": int(match.group(1)),
                "handle": int(match.group(2), 16),
                "tx_unacked": int(match.group(3)),
                "tx_flushed": int(match.group(4)),
                "tx_last_subevent": int(match.group(5)),
                "retransmitted": int(match.group(6)),
                "crc_error": int(match.group(7)),
                "rx_unreceived": int(match.group(8)),
                "duplicate": int(match.group(9)),
                "iso_interval_1250us": int(match.group(10)),
                "nse": int(match.group(11)),
                "cig_sync_us": int(match.group(12)),
                "cis_sync_us": int(match.group(13)),
                "c_max_pdu": int(match.group(14)),
                "c_phy": int(match.group(15)),
                "c_bn": int(match.group(16)),
                "c_flush_1250us": int(match.group(17)),
            }
        )
    return result


def validate_iso_link_quality(parsed, stream_count):
    """Validate one parsed ISO link-quality snapshot for a row."""
    errors = []
    if not isinstance(parsed, dict):
        return ["ISO link quality parser result missing"]
    if parsed.get("header_seen") is not True:
        errors.append("ISO link quality header missing")
    if parsed.get("malformed") is True:
        errors.append("ISO link quality grammar malformed")

    streams = parsed.get("streams")
    if not isinstance(streams, list):
        errors.append("ISO link quality stream records missing")
        return errors
    if len(streams) != stream_count:
        errors.append(
            "ISO link quality expected %d stream records, got %d"
            % (stream_count, len(streams))
        )

    seen = set()
    fields = (
        "handle",
        "tx_unacked",
        "tx_flushed",
        "tx_last_subevent",
        "retransmitted",
        "crc_error",
        "rx_unreceived",
        "duplicate",
    )
    selected_fields = (
        "iso_interval_1250us",
        "nse",
        "cig_sync_us",
        "cis_sync_us",
        "c_max_pdu",
        "c_phy",
        "c_bn",
        "c_flush_1250us",
    )
    for record in streams:
        if not isinstance(record, dict):
            errors.append("ISO link quality stream record malformed")
            continue
        slot = record.get("slot")
        if not isinstance(slot, int) or isinstance(slot, bool) or slot < 0:
            errors.append("ISO link quality slot malformed")
        elif slot >= stream_count:
            errors.append(
                "ISO link quality slot %d out of range [0, %d)" % (slot, stream_count)
            )
        elif slot in seen:
            errors.append("ISO link quality duplicate slot %d" % slot)
        else:
            seen.add(slot)
        for field in fields:
            value = record.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                errors.append("ISO link quality %s malformed" % field)
        for field in selected_fields:
            value = record.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                errors.append("ISO link quality %s malformed" % field)

    missing = [slot for slot in range(stream_count) if slot not in seen]
    if missing:
        errors.append("ISO link quality missing slot(s): %r" % missing)
    return errors


def parse_stream_summary(text):
    """Parse every stream summary in a transcript; returns a list of
    dicts in order of appearance."""
    out = []
    for match in RE_STREAM_SUMMARY.finditer(strip_vt100(text)):
        out.append(
            {
                "slot": int(match.group(1)),
                "sdus": int(match.group(2)),
                "decoded": int(match.group(3)),
                "plc": int(match.group(4)),
                "decode_err": int(match.group(5)),
                "i2s_underrun": int(match.group(6)),
                "stream_reset": int(match.group(7)),
                "empty_sdu": int(match.group(8)),
                "rx_valid": int(match.group(9)) if match.group(9) is not None else None,
                "rx_error": int(match.group(10))
                if match.group(10) is not None
                else None,
                "rx_lost": int(match.group(11))
                if match.group(11) is not None
                else None,
                "rx_unknown": int(match.group(12))
                if match.group(12) is not None
                else None,
                "rx_no_ts": int(match.group(13))
                if match.group(13) is not None
                else None,
            }
        )
    return out


def parse_flpr_handshake(text):
    """Parse the FLPR handshake block from a ``flpr status`` transcript.

    Returns a dict with ready/acked/healthy booleans and every counter;
    a missing required line leaves the field None (missing evidence,
    never zero).
    """
    result = {
        "header_seen": False,
        "ready": None,
        "acked": None,
        "healthy": None,
        "err_len": None,
        "err_ver": None,
        "err_unk": None,
        "err_send": None,
        "rx_lost": None,
        "rx_dup": None,
        "rx_ooo": None,
        "rx_missed": None,
    }
    in_block = False
    for line in strip_vt100(text).splitlines():
        if RE_HANDSHAKE_HEADER.match(line):
            in_block = True
            result["header_seen"] = True
            continue
        if not in_block:
            continue
        if line.startswith("---") or line.startswith("  State"):
            break  # block ended (offload status follows)
        match = RE_HS_BOOL.match(line)
        if match:
            result[match.group(1).lower()] = match.group(2) == "yes"
            continue
        match = RE_HS_ERRORS.match(line)
        if match:
            result["err_len"] = int(match.group(1))
            result["err_ver"] = int(match.group(2))
            result["err_unk"] = int(match.group(3))
            result["err_send"] = int(match.group(4))
            continue
        match = RE_HS_RX_LOST.match(line)
        if match:
            result["rx_lost"] = int(match.group(1))
            continue
        match = RE_HS_RX_DUP.match(line)
        if match:
            result["rx_dup"] = int(match.group(1))
            continue
        match = RE_HS_RX_OOO.match(line)
        if match:
            result["rx_ooo"] = int(match.group(1))
            continue
        match = RE_HS_RX_MISSED.match(line)
        if match:
            result["rx_missed"] = int(match.group(1))
            continue
    return result


def parse_fault_ack(text, fault):
    """Return stable named-fault ACK token from one shell transcript.

    The shell owns presentation text. Host evidence stores it verbatim, while
    recovery validation consumes a narrow stable token. Unknown fault names or
    missing/malformed acknowledgements return ``None`` and fail closed.
    """
    plain = strip_vt100(text)
    if fault == "hang" and RE_FAULT_HANG_ACK.search(plain):
        return "FAULT_HANG_ACK"
    if fault == "stall" and RE_TIMED_STALL_ACK.search(plain):
        return "TIMED_STALL_ACK"
    return None


def normalized_recovery_baseline(offload):
    """Copy a status snapshot into a fully numeric recovery baseline.

    ``flpr offload`` omits Runtime/HB lines until nonzero. Shared parser uses
    ``-1`` for absent fields, but pre-fault absence means zero for the two
    optional runtime counters. Required status fields stay explicit and are
    rejected by validation if missing.
    """
    baseline = dict(offload)
    for field in ("runtime_restarts", "runtime_fails", "hb_dedup"):
        if baseline.get(field) == -1:
            baseline[field] = 0
    return baseline


def _validate_audio_faults(audio_faults):
    errors = []
    if not isinstance(audio_faults, dict):
        return ["audio faults missing"]
    for field, label in (
        ("decode_errors", "Decode errors"),
        ("i2s_underruns", "I2S underruns"),
        ("stream_resets", "Stream resets"),
        ("push_failures", "Push failures"),
    ):
        value = audio_faults.get(field)
        if value is None:
            errors.append("%s missing" % label)
        elif value != 0:
            errors.append("%s=%r" % (label, value))
    return errors


_OFFLOAD_FAULT_FIELDS = (
    "fault_timeout",
    "fault_full",
    "fault_stale",
    "fault_seq",
    "fault_frame",
    "fault_crc",
    "fault_payload",
)


def _validate_active_offload(
    offload,
    profile="48_4_1",
    recovery=None,
    allow_moving_single_pending=False,
    allow_offload_disabled=False,
):
    """Validate active offload fields without requiring other diagnostics."""
    if not isinstance(offload, dict):
        return ["offload missing"]
    errors = []
    if offload.get("state") != "ACTIVE":
        errors.append("offload state=%r" % offload.get("state"))
    if profile == "48_4_1":
        submit = offload.get("submit")
        success = offload.get("success")
        if allow_offload_disabled:
            if not isinstance(submit, int) or isinstance(submit, bool):
                errors.append("offload submit missing or malformed")
            elif submit != 0:
                errors.append(
                    "offload submit=%d but offload-disabled run expects 0" % submit
                )
            if not isinstance(success, int) or isinstance(success, bool):
                errors.append("offload success missing or malformed")
            elif success != 0:
                errors.append(
                    "offload success=%d but offload-disabled run expects 0" % success
                )
        elif not isinstance(submit, int) or isinstance(submit, bool) or submit < 1:
            errors.append("offload submit missing or zero")
        elif recovery is None:
            moving_single_pending = (
                allow_moving_single_pending
                and isinstance(success, int)
                and not isinstance(success, bool)
                and success >= 0
                and submit == success + 1
            )
            if submit != success and not moving_single_pending:
                errors.append("offload submit/success mismatch")
    elif profile == "48_3_1":
        for field in ("submit", "success", "fallback", "busy"):
            if offload.get(field) != 0:
                errors.append("7.5 ms offload %s=%r" % (field, offload.get(field)))
    else:
        raise ReceiverError("unsupported receiver validation profile %r" % profile)
    for field in (
        "fallback",
        "busy",
        "recovery_attempts",
        "recovery_fail",
        "relapses",
        "exhaustion",
        "probation_active",
    ):
        value = offload.get(field)
        if value is None or value == -1:
            errors.append("offload %s missing" % field)
        elif recovery is None and profile == "48_4_1" and value != 0:
            errors.append("offload %s=%r" % (field, value))
    if recovery is not None:
        errors.extend(_validate_recovery(offload, recovery))
    for field in _OFFLOAD_FAULT_FIELDS:
        value = offload.get(field)
        if value is None or value == -1:
            errors.append("offload %s missing" % field)
        elif recovery is not None and field in ("fault_timeout", "fault_full"):
            # Named hang/stall injection produces expected transport evidence.
            # Its delta is verified below; it must not turn into a global
            # warning/fault exemption for healthy rows.
            continue
        elif value != 0:
            errors.append("offload %s=%r" % (field, value))
    return errors


def _validate_handshake(handshake):
    errors = []
    if not isinstance(handshake, dict):
        return ["handshake missing"]
    for field, label in (
        ("ready", "Ready"),
        ("acked", "ACKed"),
        ("healthy", "Healthy"),
    ):
        if handshake.get(field) is not True:
            errors.append("handshake %s=%r" % (label, handshake.get(field)))
    for field, label in (
        ("err_len", "errors len"),
        ("err_ver", "errors ver"),
        ("err_unk", "errors unk"),
        ("err_send", "errors send"),
        ("rx_lost", "RX lost"),
        ("rx_dup", "RX dup"),
        ("rx_ooo", "RX ooo"),
        ("rx_missed", "RX missed"),
    ):
        value = handshake.get(field)
        if value is None:
            errors.append("handshake %s missing" % label)
        elif value != 0:
            errors.append("handshake %s=%r" % (label, value))
    return errors


def validate_receiver_blocks(
    audio_faults,
    offload,
    handshake,
    profile="48_4_1",
    recovery=None,
    allow_moving_single_pending=False,
):
    """Validate one live receiver status snapshot.

    Requires Decode errors, I2S underruns, Stream resets, and Push
    failures all present and zero. A 10 ms ``48_4_1`` healthy row requires live
    FLPR offload with submit == success and zero fallback/fault/recovery
    counters. A caller may permit one ``submit == success + 1`` snapshot only
    after separately proving that the pending transaction is moving. A 7.5 ms
    ``48_3_1`` healthy row deliberately uses CPUAPP ASRC because current FLPR
    input payloads accept 480, not 360, frames. It still requires a healthy
    idle FLPR plane and zero offload counters, but must not falsely demand
    offload submissions.

    ``recovery`` is ``None`` for healthy rows. Named FLPR fault rows pass a
    validated recovery snapshot so expected transport timeout/full and fallback
    evidence does not weaken every other integrity rule. Missing evidence
    always fails.
    """
    if profile not in ("48_4_1", "48_3_1"):
        raise ReceiverError("unsupported receiver validation profile %r" % profile)
    errors = []
    errors.extend(_validate_audio_faults(audio_faults))
    errors.extend(
        _validate_active_offload(
            offload,
            profile=profile,
            recovery=recovery,
            allow_moving_single_pending=allow_moving_single_pending,
        )
    )
    errors.extend(_validate_handshake(handshake))
    return errors


def _is_nonnegative_int(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def _validate_post_stop_offload(
    active_offload,
    post_stop_offload,
    profile,
    recovery=None,
    allow_offload_disabled=False,
):
    """Validate terminal offload lifecycle and accepted-submit accounting.

    Healthy streams terminate every accepted submit as an offload success.
    Named fault rows retain CPUAPP fallback evidence, so their terminal
    partition is ``submit == success + fallback``.
    """
    errors = []
    if not isinstance(post_stop_offload, dict):
        return ["post-stop offload missing"]
    if post_stop_offload.get("state") != "STOPPED":
        errors.append("post-stop offload state=%r" % post_stop_offload.get("state"))

    submit = post_stop_offload.get("submit")
    success = post_stop_offload.get("success")
    fallback = post_stop_offload.get("fallback")
    submit_valid = _is_nonnegative_int(submit)
    success_valid = _is_nonnegative_int(success)
    fallback_valid = _is_nonnegative_int(fallback)
    if not submit_valid:
        errors.append("post-stop offload submit missing or malformed")
    if not success_valid:
        errors.append("post-stop offload success missing or malformed")
    if (
        submit_valid
        and success_valid
        and isinstance(submit, int)
        and isinstance(success, int)
    ):
        if recovery is None and submit != success:
            errors.append("post-stop offload submit/success mismatch")
        elif recovery is not None and fallback_valid and isinstance(fallback, int):
            if submit != success + fallback:
                errors.append("post-stop offload submit/success/fallback mismatch")
        if profile == "48_4_1":
            if allow_offload_disabled:
                if submit != 0:
                    errors.append(
                        "post-stop offload submit=%d but offload-disabled run expects 0"
                        % submit
                    )
                if success != 0:
                    errors.append(
                        "post-stop offload success=%d but offload-disabled run expects 0"
                        % success
                    )
            elif (
                recovery is None
                and isinstance(submit, int)
                and not isinstance(submit, bool)
                and submit < 1
            ):
                errors.append("post-stop offload submit missing or zero")
        elif profile == "48_3_1":
            for field in ("submit", "success"):
                if post_stop_offload.get(field) != 0:
                    errors.append(
                        "7.5 ms post-stop offload %s=%r"
                        % (field, post_stop_offload.get(field))
                    )
        else:
            raise ReceiverError("unsupported receiver validation profile %r" % profile)

    if profile == "48_4_1":
        active_submit = (
            active_offload.get("submit") if isinstance(active_offload, dict) else None
        )
        active_success = (
            active_offload.get("success") if isinstance(active_offload, dict) else None
        )
        if not _is_nonnegative_int(active_submit):
            errors.append("active offload submit missing or malformed")
        elif (
            submit_valid
            and isinstance(submit, int)
            and not isinstance(submit, bool)
            and isinstance(active_submit, int)
            and not isinstance(active_submit, bool)
            and submit < active_submit
        ):
            errors.append("post-stop offload submit regressed")
        if not _is_nonnegative_int(active_success):
            errors.append("active offload success missing or malformed")
        elif (
            success_valid and isinstance(success, int) and not isinstance(success, bool)
        ):
            if (
                not allow_offload_disabled
                and recovery is None
                and isinstance(active_success, int)
                and not isinstance(active_success, bool)
                and success <= active_success
            ):
                errors.append("post-stop offload success did not advance")
            elif (
                recovery is not None
                and isinstance(active_success, int)
                and not isinstance(active_success, bool)
                and success < active_success
            ):
                errors.append("post-stop offload success regressed")

    for field in ("fallback", "busy"):
        value = post_stop_offload.get(field)
        if value is None or value == -1:
            errors.append("post-stop offload %s missing" % field)
        elif not _is_nonnegative_int(value):
            errors.append("post-stop offload %s malformed" % field)
        elif recovery is None and value != 0:
            errors.append("post-stop offload %s=%r" % (field, value))
        elif profile == "48_3_1" and value != 0:
            errors.append("7.5 ms post-stop offload %s=%r" % (field, value))

    recovery_fields = (
        "recovery_attempts",
        "recovery_fail",
        "relapses",
        "exhaustion",
        "probation_active",
    )
    for field in recovery_fields:
        value = post_stop_offload.get(field)
        if value is None or value == -1:
            errors.append("post-stop offload %s missing" % field)
        elif not _is_nonnegative_int(value):
            errors.append("post-stop offload %s malformed" % field)
        elif recovery is None and value != 0:
            errors.append("post-stop offload %s=%r" % (field, value))

    for field in _OFFLOAD_FAULT_FIELDS:
        value = post_stop_offload.get(field)
        if value is None or value == -1:
            errors.append("post-stop offload %s missing" % field)
        elif not _is_nonnegative_int(value):
            errors.append("post-stop offload %s malformed" % field)
        elif recovery is None and value != 0:
            errors.append("post-stop offload %s=%r" % (field, value))

    if recovery is not None and isinstance(active_offload, dict):
        active_compare = normalized_recovery_baseline(active_offload)
        final_compare = normalized_recovery_baseline(post_stop_offload)
        # Normal stream stop resets probation-success progress; cleared count remains lifetime evidence.
        for field in (
            "fallback",
            "busy",
            "recovery_attempts",
            "recovery_fail",
            "relapses",
            "exhaustion",
            "probation_active",
            "probation_cleared",
            "runtime_restarts",
            "runtime_fails",
            "hb_dedup",
        ) + _OFFLOAD_FAULT_FIELDS:
            active_value = active_compare.get(field)
            final_value = final_compare.get(field)
            if final_value is None or final_value == -1:
                errors.append("post-stop offload %s missing" % field)
            elif active_value != final_value:
                errors.append("post-stop offload %s changed" % field)
    return errors


def validate_receiver_lifecycle_blocks(
    audio_faults,
    active_offload,
    post_stop_offload,
    handshake,
    profile="48_4_1",
    recovery=None,
    allow_moving_single_pending=False,
    allow_offload_disabled=False,
):
    """Validate active receiver evidence and its terminal stopped snapshot."""
    if profile not in ("48_4_1", "48_3_1"):
        raise ReceiverError("unsupported receiver validation profile %r" % profile)
    errors = []
    errors.extend(_validate_audio_faults(audio_faults))
    errors.extend(
        _validate_active_offload(
            active_offload,
            profile=profile,
            recovery=recovery,
            allow_moving_single_pending=allow_moving_single_pending,
            allow_offload_disabled=allow_offload_disabled,
        )
    )
    errors.extend(_validate_handshake(handshake))
    errors.extend(
        _validate_post_stop_offload(
            active_offload,
            post_stop_offload,
            profile,
            recovery=recovery,
            allow_offload_disabled=allow_offload_disabled,
        )
    )
    return errors


def _validate_recovery(offload, recovery):
    """Validate named FLPR recovery outcome without accepting unrelated faults.

    ``hang`` needs runtime restart after heartbeat loss. ``stall`` needs a
    timed-stall ACK and automatic recovery, but does not require a runtime
    restart because short ring reset can recover it. Both require fresh
    recovery evidence relative to a runner-owned baseline, ACTIVE after
    probation, fallback evidence, no exhaustion/relapse/failure, and no
    integrity fault. The expected transport timeout/full counters remain
    narrow fault-window evidence only.
    """
    if not isinstance(recovery, dict):
        return ["recovery evidence missing"]
    fault = recovery.get("fault")
    if fault not in ("hang", "stall"):
        return ["recovery fault=%r" % fault]
    errors = []
    baseline = recovery.get("baseline")
    if not isinstance(baseline, dict):
        return ["recovery baseline missing"]
    for field in (
        "recovery_attempts",
        "recovery_fail",
        "relapses",
        "exhaustion",
        "probation_cleared",
        "runtime_restarts",
        "epoch",
        "success",
        "fallback",
    ):
        if not isinstance(baseline.get(field), int) or baseline[field] < 0:
            errors.append("recovery baseline %s missing" % field)
    if errors:
        return errors
    ack = recovery.get("ack")
    if fault == "hang":
        if ack != "FAULT_HANG_ACK":
            errors.append("hang ACK missing")
    elif ack != "TIMED_STALL_ACK":
        errors.append("timed stall ACK missing")
    if offload.get("recovery_attempts") != baseline["recovery_attempts"] + 1:
        errors.append("recovery attempts delta")
    if offload.get("recovery_fail") != baseline["recovery_fail"]:
        errors.append("recovery fail changed")
    if offload.get("relapses") != baseline["relapses"]:
        errors.append("recovery relapses changed")
    if offload.get("exhaustion") != baseline["exhaustion"]:
        errors.append("recovery exhaustion changed")
    if offload.get("probation_active") != 0:
        errors.append("recovery probation active")
    if offload.get("probation_cleared", -1) < baseline["probation_cleared"] + 1:
        errors.append("recovery probation not cleared")
    if offload.get("epoch") == baseline["epoch"]:
        errors.append("recovery epoch unchanged")
    if offload.get("fallback", -1) <= baseline["fallback"]:
        errors.append("recovery fallback missing")
    if offload.get("success", -1) <= baseline["success"]:
        errors.append("recovery success did not resume")
    if fault == "hang":
        if offload.get("runtime_restarts") != baseline["runtime_restarts"] + 1:
            errors.append("hang runtime restart delta")
    elif offload.get("runtime_restarts") != baseline["runtime_restarts"]:
        errors.append("stall runtime restart changed")
    return errors


def run_receiver_command(
    console, text, prompt=RECEIVER_PROMPT, timeout=15.0, cancel=None
):
    """One prompt-bounded receiver shell command; returns the transcript.
    Raises ``ReceiverError`` on timeout without prompt return."""
    try:
        return console.command_receiver(text, prompt, timeout, cancel=cancel)
    except SerialConsoleCancelled:
        raise
    except SerialConsoleError as exc:
        raise ReceiverError(str(exc)) from exc


def scan_warnings(lines):
    """Scan source unprefixed lines or receiver raw log lines for any
    warning signature; returns the list of offending lines."""
    hits = []
    for line in lines:
        plain_line = strip_vt100(line)
        for pattern in WARNING_PATTERNS:
            if pattern.search(plain_line):
                hits.append(line)
                break
    return hits


def iter_raw_utf8_lines(raw):
    """Yield ``(start_offset, end_offset, line, complete)`` from raw UTF-8.

    Offsets are half-open raw-byte ranges. ``end_offset`` includes any line
    terminator, so a line qualifies for an exemption only when every byte that
    formed it arrived inside the runner-recorded window. Text splitting matches
    the existing ``str.splitlines()`` warning scan semantics while UTF-8
    re-encoding each decoded segment preserves its original byte length.
    """
    text = raw.decode("utf-8")
    offset = 0
    for segment in text.splitlines(keepends=True):
        raw_segment = segment.encode("utf-8")
        end_offset = offset + len(raw_segment)
        if segment.endswith("\r\n"):
            line = segment[:-2]
            complete = True
        elif segment and segment[-1] in _LINE_ENDINGS:
            line = segment[:-1]
            complete = True
        else:
            line = segment
            complete = False
        yield offset, end_offset, line, complete
        offset = end_offset


def _raw_window(recovery, raw_length):
    """Return a valid recovery raw-byte window, else ``None``.

    A valid cursor range is bounded by retained raw evidence. Bool values are
    deliberately rejected even though Python treats them as integers.
    """
    if not isinstance(recovery, dict):
        return None
    window = recovery.get("raw_window")
    if not isinstance(window, dict) or set(window) != {"start_offset", "end_offset"}:
        return None
    start_offset = window.get("start_offset")
    end_offset = window.get("end_offset")
    if not isinstance(start_offset, int) or isinstance(start_offset, bool):
        return None
    if not isinstance(end_offset, int) or isinstance(end_offset, bool):
        return None
    if start_offset < 0 or end_offset < start_offset or end_offset > raw_length:
        return None
    return start_offset, end_offset


def _is_documented_hang_recovery_warning(line):
    """True only for one exact documented warning payload and log envelope."""
    plain_line = strip_vt100(line).strip()
    envelope = RE_HANG_RECOVERY_WARNING_ENVELOPE.fullmatch(plain_line)
    if envelope is None:
        return False
    module = envelope.group("module")
    payload = envelope.group("payload")
    return any(
        module == expected_module and pattern.fullmatch(payload)
        for expected_module, pattern in HANG_RECOVERY_WARNING_PAYLOADS
    )


def scan_raw_warnings(raw, fault=None, recovery=None):
    """Scan receiver raw evidence, applying only a bounded hang exemption.

    The existing ``scan_warnings()`` matcher remains authority for what counts
    as a warning. A warning is removed only when the row is ``hang``, recovery
    evidence carries a valid raw-byte window, the complete raw line is inside
    that window, and its stripped module/payload pair is one of the four documented
    hang-recovery diagnostics. Fault rows without valid window evidence fail
    closed before any warning can be exempted.
    """
    raw_lines = tuple(iter_raw_utf8_lines(raw))
    window = None
    if fault is not None:
        window = _raw_window(recovery, len(raw))
        if window is None:
            raise ReceiverError("fault recovery raw_window missing or invalid")

    hits = []
    for line_start, line_end, line, complete in raw_lines:
        if not scan_warnings((line,)):
            continue
        if (
            fault == "hang"
            and window is not None
            and window[0] <= line_start
            and line_end <= window[1]
            and complete
            and _is_documented_hang_recovery_warning(line)
        ):
            continue
        hits.append(line)
    return hits


def scan_shell_errors(transcript):
    """Scan one prompt-bounded receiver transcript for shell errors.

    Do not apply this parser to the entire raw boot/run log. The command
    collector already bounds each transcript after its echo and returned
    prompt, so this rejects command failures without mistaking arbitrary
    firmware log prose for a shell failure.
    """
    hits = []
    for line in strip_vt100(transcript).splitlines():
        if line.strip() and RE_SHELL_ERROR.search(line.strip()):
            if "Identity unavailable." in line:
                continue
            hits.append(line)
    return hits
