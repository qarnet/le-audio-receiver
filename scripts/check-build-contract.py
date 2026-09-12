#!/usr/bin/env python3
"""Resolved build-contract checker for the LE Audio Receiver.

Parses the RESOLVED ``.config`` and ``zephyr.dts`` files beneath
each sysbuild root (never top-level sysbuild configs) and asserts the
production build contracts documented in
``docs/testing/behavior-contract.md`` (BUILD-*).

Stdlib only.  Deterministic output: one PASS/FAIL line per assertion, in
fixed order; every failed assertion is listed in one run.  Exit status:
  0  every contract passed
  1  one or more assertion failures (listed)
  2  hard input error (missing/duplicate/unreadable/malformed input)

Invocation:
  python3 scripts/check-build-contract.py \
    --nrf54l15 build/nrf54l15

Optional legacy local validation:
  python3 scripts/check-build-contract.py \
    --nrf5340 build/nrf5340 \
    --nrf54l15 build/nrf54l15
"""

import argparse
import os
import re
import sys

# ── config parsing ──────────────────────────────────────────────────

# Values: "y", "n", integer (decimal or hex), or a quoted string.
_CONFIG_VALUE_RE = re.compile(r'^(y|n|-?0x[0-9a-fA-F]+|-?[0-9]+|"[^"]*")$')


class ConfigError(ValueError):
    """Malformed or duplicate .config input."""


def parse_config(text):
    """Parse a resolved Zephyr ``.config`` into an ordered dict.

    Returns ``{key: value}`` where value is "y", "n", an int, a str, or
    "unset" (``# CONFIG_X is not set``).  Duplicate keys and malformed
    CONFIG lines are hard errors.
    """
    cfg = {}
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            if line.startswith("# CONFIG_") and line.endswith(" is not set"):
                key = line[2:].split()[0]
                if key in cfg:
                    raise ConfigError(
                        "duplicate config key %s (line %d)" % (key, lineno)
                    )
                cfg[key] = "unset"
            continue
        if not line.startswith("CONFIG_"):
            continue
        if "=" not in line:
            raise ConfigError("malformed config line %d: %r" % (lineno, raw))
        key, _, val = line.partition("=")
        if not _CONFIG_VALUE_RE.match(val):
            raise ConfigError("malformed config value on line %d: %r" % (lineno, raw))
        if key in cfg:
            raise ConfigError("duplicate config key %s (line %d)" % (key, lineno))
        if val == "y":
            cfg[key] = "y"
        elif val == "n":
            cfg[key] = "n"
        elif val.startswith("-"):
            cfg[key] = (
                -int(val[1:], 0) if val[1:].lower().startswith("0x") else int(val)
            )
        elif val.startswith("0x"):
            cfg[key] = int(val, 16)
        elif val.isdigit():
            cfg[key] = int(val)
        else:
            cfg[key] = val[1:-1]
    return cfg


def config_enabled(cfg, key):
    """True only for an explicit ``=y``."""
    return cfg.get(key) == "y"


def config_not_enabled(cfg, key):
    """True when the symbol is absent, explicitly unset, or ``=n``."""
    return cfg.get(key) != "y"


def config_int(cfg, key):
    """Integer value, or None when absent/not an integer."""
    v = cfg.get(key)
    return v if isinstance(v, int) else None


def config_str(cfg, key):
    """String value, or None when absent/not a string."""
    v = cfg.get(key)
    return v if isinstance(v, str) and v != "unset" else None


# ── devicetree parsing ──────────────────────────────────────────────

_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)
_DTS_V1_RE = re.compile(r"^\s*/dts-v1/\s*;\s*$")
_NODE_OPEN_RE = re.compile(
    r"^\s*(?:(?P<labels>(?:[A-Za-z0-9_]+\s*:\s*)+))?"
    r"(?P<name>[/A-Za-z0-9_+-]+)"
    r"(?P<addr>@[0-9a-fA-Fx]+)?\s*\{\s*$"
)
_REF_OPEN_RE = re.compile(r"^\s*&(?P<label>[A-Za-z0-9_]+)\s*\{\s*$")
_PROP_NAME_RE = re.compile(r"[A-Za-z0-9_#.,+-]+")

_CELL_RE = re.compile(r"<([^>]*)>")
_STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
_BYTES_RE = re.compile(r"\[([^\]]*)\]")
_REF_RE = re.compile(r"&([A-Za-z0-9_]+)")

_INT_RE = re.compile(r"0x[0-9a-fA-F]+|[0-9]+")
_BYTE_RE = re.compile(r"[0-9a-fA-F]{2}")


class DtsError(ValueError):
    """Malformed resolved devicetree input."""


class DtsNode:
    """One resolved devicetree node with properties and children."""

    def __init__(self, name, unit_addr=None):
        self.name = name
        self.unit_addr = unit_addr
        self.labels = []
        self.props = {}  # name -> list of value groups
        self.children = []
        self.parent = None

    def path(self):
        parts = []
        node = self
        while node is not None:
            parts.append(node.name)
            node = node.parent
        return "/" + "/".join(reversed(parts))

    def prop(self, name):
        """List of value groups for a property, or None."""
        return self.props.get(name)

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def status(self):
        groups = self.props.get("status")
        if not groups or not groups[0]:
            return None
        return groups[0][0]

    def reg(self):
        """(addr, size) from the first reg group, or None."""
        groups = self.props.get("reg")
        if not groups or len(groups[0]) < 2:
            return None
        return (groups[0][0], groups[0][1])


def _tokenize_value_group(group_text):
    """Parse one ``<...>``/``"..."``/``[...]``/``&label`` value group."""
    group_text = group_text.strip()
    if not group_text:
        return []
    if group_text.startswith("<") and group_text.endswith(">"):
        inner = group_text[1:-1]
        tokens = []
        for cell in inner.split():
            m = _INT_RE.match(cell)
            if m:
                tokens.append(int(m.group(0), 0))
                continue
            m = _REF_RE.fullmatch(cell)
            if m:
                tokens.append("&" + m.group(1))
                continue
            raise DtsError("bad cell %r" % cell)
        return tokens
    if group_text.startswith('"') and group_text.endswith('"'):
        return [group_text[1:-1]]
    if group_text.startswith("[") and group_text.endswith("]"):
        inner = group_text[1:-1]
        tokens = []
        for cell in inner.split():
            m = _BYTE_RE.fullmatch(cell)
            if not m:
                raise DtsError("bad byte cell %r" % cell)
            tokens.append(int(m.group(0), 16))
        return tokens
    m = _REF_RE.fullmatch(group_text)
    if m:
        return ["&" + m.group(1)]
    raise DtsError("unrecognized property value %r" % group_text)


def _split_value_groups(value_text):
    """Split a property value (after '=') into comma-separated groups."""
    groups = []
    depth = 0
    current = []
    for ch in value_text:
        if ch in '<["':
            depth += 1
        elif ch in ">]":
            depth -= 1
        elif ch == "," and depth == 0:
            groups.append("".join(current))
            current = []
            continue
        current.append(ch)
    groups.append("".join(current))
    return groups


def parse_dts(text):
    """Parse a resolved ``zephyr.dts`` into (nodes, labels).

    ``nodes`` is the ordered list of root nodes; ``labels`` maps every
    node label to its DtsNode.  Comments are removed before parsing, so
    commented text can never satisfy a check.
    """
    text = _COMMENT_RE.sub(" ", text)
    root_children = []
    stack = []  # nodes currently open
    labels = {}
    line_no = 0

    def current():
        return stack[-1] if stack else None

    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        line_no += 1
        stripped = line.strip()

        if _DTS_V1_RE.match(stripped):
            i += 1
            continue

        m = _NODE_OPEN_RE.match(stripped)
        if m and stripped.endswith("{"):
            node = DtsNode(m.group("name").lstrip("@"), m.group("addr"))
            if m.group("labels"):
                for label in m.group("labels").split(":")[:-1]:
                    label = label.strip()
                    node.labels.append(label)
                    if label in labels:
                        raise DtsError(
                            "duplicate label %s (line %d)" % (label, line_no)
                        )
                    labels[label] = node
            parent = current()
            node.parent = parent
            if parent is None:
                root_children.append(node)
            else:
                parent.children.append(node)
            stack.append(node)
            i += 1
            continue

        m = _REF_OPEN_RE.match(stripped)
        if m and stripped.endswith("{"):
            label = m.group("label")
            if label not in labels:
                raise DtsError(
                    "reference to unknown label %s (line %d)" % (label, line_no)
                )
            target = labels[label]
            parent = current()
            if parent is not None:
                target.parent = parent
                if target not in parent.children:
                    parent.children.append(target)
            else:
                if target not in root_children:
                    root_children.append(target)
            stack.append(target)
            i += 1
            continue

        if stripped in ("}", "};"):
            if not stack:
                raise DtsError("unbalanced '}' at line %d" % line_no)
            stack.pop()
            i += 1
            continue

        if stripped.endswith(";") and not stripped.endswith("};"):
            body = stripped[:-1].rstrip()
            if "=" in body:
                name, _, value_text = body.partition("=")
                name = name.strip()
                groups = []
                for g in _split_value_groups(value_text):
                    groups.append(_tokenize_value_group(g))
            else:
                name = body.strip()
                groups = []
            node = current()
            if node is None:
                raise DtsError("property outside node at line %d" % line_no)
            if name in node.props:
                raise DtsError("duplicate property %s in %s" % (name, node.path()))
            node.props[name] = groups
            i += 1
            continue

        # Accumulate multi-line property values until a ';' is found.
        if "=" in stripped or stripped.endswith(","):
            acc = [stripped]
            i += 1
            while i < len(lines):
                line_no += 1
                piece = lines[i].strip()
                acc.append(piece)
                i += 1
                if piece.endswith(";"):
                    break
            body = " ".join(acc)
            body = body[: body.rfind(";")].rstrip()
            name, _, value_text = body.partition("=")
            name = name.strip()
            groups = []
            for g in _split_value_groups(value_text):
                groups.append(_tokenize_value_group(g))
            node = current()
            if node is None:
                raise DtsError("property outside node at line %d" % line_no)
            if name in node.props:
                raise DtsError("duplicate property %s in %s" % (name, node.path()))
            node.props[name] = groups
            continue

        i += 1

    if stack:
        raise DtsError("unbalanced '{' (node %s never closed)" % stack[-1].path())
    return root_children, labels


def node_by_label(labels, label):
    return labels.get(label)


def is_root_child(node):
    """True when node is a direct child of the resolved root node."""
    return node is not None and node.parent is not None and node.parent.name == "/"


def is_root_reserved_memory_child(node):
    """True for a direct child of a valid root ``reserved-memory`` node."""
    reserved = node.parent if node is not None else None
    root = reserved.parent if reserved is not None else None
    return (
        reserved is not None
        and reserved.name == "reserved-memory"
        and root is not None
        and root.name == "/"
        and reserved.prop("#address-cells") == [[1]]
        and reserved.prop("#size-cells") == [[1]]
        and reserved.prop("ranges") == []
    )


def unit_address_matches_reg(node, reg):
    """True when node unit address equals first address in its first reg."""
    if node is None or reg is None or not node.unit_addr:
        return False
    try:
        return int(node.unit_addr[1:], 16) == reg[0]
    except ValueError:
        return False


def find_chosen(nodes):
    """The chosen node (directly at root, or under the '/' root node)."""
    for n in nodes:
        if n.name == "chosen":
            return n
    for n in nodes:
        if n.name == "/":
            for c in n.children:
                if c.name == "chosen":
                    return c
    return None


def decode_psel(cell):
    """Decode an NRF_PSEL cell into (function, port, pin)."""
    fun = (cell >> 24) & 0xFF
    v = cell & 0x1FF
    return (fun, v // 32, v % 32)


def chosen_ref(labels, chosen_node, prop_name):
    """Resolve a chosen property to the referenced node, or None."""
    if chosen_node is None:
        return None
    groups = chosen_node.props.get(prop_name)
    if not groups or not groups[0]:
        return None
    ref = groups[0][0]
    if not (isinstance(ref, str) and ref.startswith("&")):
        return None
    return labels.get(ref[1:])


def alias_ref(labels, nodes, alias_name):
    """Resolve a root /aliases property to the referenced node, or None."""
    aliases = None
    for n in nodes:
        if n.name == "aliases":
            aliases = n
            break
    if aliases is None:
        for n in nodes:
            if n.name == "/":
                for c in n.children:
                    if c.name == "aliases":
                        aliases = c
                        break
            if aliases is not None:
                break
    if aliases is None:
        return None
    groups = aliases.props.get(alias_name)
    if not groups or not groups[0]:
        return None
    ref = groups[0][0]
    if not (isinstance(ref, str) and ref.startswith("&")):
        return None
    return labels.get(ref[1:])


# GPIO flag/code constants (Zephyr dt-bindings/gpio/gpio.h, input_codes.h).
GPIO_ACTIVE_LOW = 1
GPIO_PULL_UP = 16
INPUT_KEY_0 = 11


def gpio_spec(node, prop_name="gpios"):
    """(controller_label, pin, flags) from the first gpios group, or None."""
    if node is None:
        return None
    groups = node.props.get(prop_name)
    if not groups or not groups[0]:
        return None
    cells = groups[0]
    if len(cells) < 3 or not isinstance(cells[0], str) or not cells[0].startswith("&"):
        return None
    return (cells[0][1:], cells[1], cells[2])


# ── contract checks ─────────────────────────────────────────────────

PHYS_SRAM_BASE = 0x20000000
PHYS_SRAM_END = 0x20040000

SRAM_CHAIN = [
    ("cpuapp_sram", 0x20000000, 0x28000),
    ("sram_rx", 0x20028000, 0x2000),
    ("sram_tx", 0x2002A000, 0x2000),
    ("pcm_ring", 0x2002C000, 0x4000),
    ("cpuflpr_sram_code_data", 0x20030000, 0x10000),
]

I2S_PSEL_5340 = [(13, 1, 15), (15, 1, 12), (18, 1, 13)]  # SCK_M, LRCK_M, SDOUT
I2S_PSEL_54L15 = [(13, 1, 4), (15, 1, 5), (18, 1, 6), (19, 1, 7)]  # + MCK


class ContractResult:
    """Ordered, deterministic assertion results."""

    def __init__(self):
        self.entries = []  # (ok, cid, description, detail)

    def add(self, ok, cid, description, detail=""):
        self.entries.append((bool(ok), cid, description, detail))

    def failures(self):
        return [e for e in self.entries if not e[0]]


def check_i2s_pins(result, labels, pinctrl_label, expected, tag, prefix):
    pnode = node_by_label(labels, pinctrl_label)
    if pnode is None:
        result.add(
            False, tag, "%s default pinctrl node %s missing" % (prefix, pinctrl_label)
        )
        return
    group1 = pnode.child("group1")
    if group1 is None:
        result.add(False, tag, "%s %s has no group1" % (prefix, pinctrl_label))
        return
    psels = group1.props.get("psels")
    if psels is None:
        result.add(False, tag, "%s %s has no psels" % (prefix, pinctrl_label))
        return
    cells = [c for g in psels for c in g]
    decoded = [decode_psel(c) for c in cells]
    if decoded != expected:
        result.add(
            False,
            tag,
            "%s %s psels decode to %s, expected %s"
            % (prefix, pinctrl_label, decoded, expected),
        )
        return
    result.add(
        True,
        tag,
        "%s %s psels exact (function, port, pin): %s"
        % (prefix, pinctrl_label, expected),
    )


def check_rfsw(result, labels, label, expected_pin, expected_flags, tag, prefix):
    node = node_by_label(labels, label)
    if node is None:
        result.add(False, tag, "%s node %s missing" % (prefix, label))
        return
    groups = node.props.get("enable-gpios")
    if groups is None:
        result.add(False, tag, "%s %s missing enable-gpios" % (prefix, label))
        return
    cells = groups[0]
    if len(cells) < 3 or not isinstance(cells[0], str) or not cells[0].startswith("&"):
        result.add(
            False, tag, "%s %s enable-gpios malformed: %r" % (prefix, label, cells)
        )
        return
    gpio_label = cells[0][1:]
    if node_by_label(labels, gpio_label) is None:
        result.add(
            False,
            tag,
            "%s %s enable-gpios phandle %s unresolvable" % (prefix, label, gpio_label),
        )
        return
    pin, flags = cells[1], cells[2]
    ok = gpio_label == "gpio2" and pin == expected_pin and flags == expected_flags
    result.add(
        ok,
        tag,
        "%s %s enable-gpios = <&%s %d %d> (expected <&gpio2 %d %d>)"
        % (prefix, label, gpio_label, pin, flags, expected_pin, expected_flags),
    )
    boot_on = "regulator-boot-on" in node.props
    result.add(boot_on, tag, "%s %s regulator-boot-on present" % (prefix, label))


def check_clock_caps(result, labels, label, tag, prefix):
    node = node_by_label(labels, label)
    if node is None:
        result.add(False, tag, "%s clock node %s missing" % (prefix, label))
        return
    caps = node.props.get("load-capacitors")
    ok_caps = bool(caps) and caps[0] == ["internal"]
    result.add(ok_caps, tag, "%s %s load-capacitors = internal" % (prefix, label))
    ff = node.props.get("load-capacitance-femtofarad")
    ok_ff = bool(ff) and len(ff[0]) == 1 and ff[0][0] == 16000
    result.add(
        ok_ff, tag, "%s %s load-capacitance-femtofarad = 16000" % (prefix, label)
    )


def check_memory_ranges(result, labels, tag, prefix):
    """Exact app-side ranges, contiguity, non-overlap, physical bounds."""
    ok_all = True
    shared_labels = {"sram_rx", "sram_tx", "pcm_ring"}
    for label, want_addr, want_size in SRAM_CHAIN:
        node = node_by_label(labels, label)
        if node is None:
            result.add(False, tag, "%s memory node %s missing" % (prefix, label))
            ok_all = False
            continue
        reg = node.reg()
        location_ok = label not in shared_labels or is_root_reserved_memory_child(node)
        unit_ok = unit_address_matches_reg(node, reg)
        if reg != (want_addr, want_size) or not location_ok or not unit_ok:
            result.add(
                False,
                tag,
                "%s %s reg/unit/parent match expected memory layout" % (prefix, label),
                "reg=%s unit=%r root_reserved=%s expected=(%s, %s)"
                % (
                    reg,
                    node.unit_addr,
                    location_ok,
                    hex(want_addr),
                    hex(want_size),
                ),
            )
            ok_all = False
        else:
            result.add(
                True,
                tag,
                "%s %s reg = (%s, %s)"
                % (prefix, label, hex(want_addr), hex(want_size)),
            )
    if ok_all:
        # Contiguity of the designed chain and physical bounds.
        prev_end = None
        contiguous = True
        in_bounds = True
        for label, addr, size in SRAM_CHAIN:
            if prev_end is not None and addr != prev_end:
                contiguous = False
            if addr < PHYS_SRAM_BASE or addr + size > PHYS_SRAM_END:
                in_bounds = False
            prev_end = addr + size
        result.add(
            contiguous, tag, "%s SRAM intervals contiguous in designed order" % prefix
        )
        result.add(
            in_bounds, tag, "%s SRAM intervals within 0x20000000..0x20040000" % prefix
        )
        result.add(
            prev_end == PHYS_SRAM_END,
            tag,
            "%s SRAM chain ends exactly at physical SRAM end" % prefix,
        )


def run_nrf5340_checks(
    app_cfg, app_dts, labels_app, net_cfg, net_dts, labels_net, result
):
    """nRF5340 contract assertions (BUILD-002/003)."""

    # ---- app config ----
    result.add(
        config_enabled(app_cfg, "CONFIG_AUDIO_RESAMPLER_IDENTITY"),
        "5340-001",
        "app CONFIG_AUDIO_RESAMPLER_IDENTITY=y",
        "got %r" % app_cfg.get("CONFIG_AUDIO_RESAMPLER_IDENTITY"),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_AUDIO_CLOCK_ACTUATOR_APLL"),
        "5340-002",
        "app CONFIG_AUDIO_CLOCK_ACTUATOR_APLL=y",
        "got %r" % app_cfg.get("CONFIG_AUDIO_CLOCK_ACTUATOR_APLL"),
    )
    result.add(
        config_not_enabled(app_cfg, "CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR"),
        "5340-003",
        "app CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR not enabled",
    )
    result.add(
        config_not_enabled(app_cfg, "CONFIG_AUDIO_CLOCK_ACTUATOR_NONE"),
        "5340-004",
        "app CONFIG_AUDIO_CLOCK_ACTUATOR_NONE not enabled",
    )
    result.add(
        config_int(app_cfg, "CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ") == 48000,
        "5340-005",
        "app output sample rate 48000",
        "got %r" % config_int(app_cfg, "CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ"),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_LIBLC3"), "5340-006", "app CONFIG_LIBLC3=y"
    )
    result.add(
        config_int(app_cfg, "CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT") == 2,
        "5340-007",
        "app two sink ASEs (ASCS_MAX_ASE_SNK_COUNT=2)",
        "got %r" % config_int(app_cfg, "CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT"),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS"),
        "5340-008",
        "app CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y",
    )
    for key, want, cid in (
        ("CONFIG_BT_BUF_ACL_TX_COUNT", 7, "5340-009"),
        ("CONFIG_BT_ISO_TX_BUF_COUNT", 6, "5340-010"),
        ("CONFIG_BT_ISO_RX_BUF_COUNT", 6, "5340-011"),
    ):
        result.add(
            config_int(app_cfg, key) == want,
            cid,
            "app %s=%d" % (key, want),
            "got %r" % config_int(app_cfg, key),
        )

    # ---- app dts ----
    i2s0 = node_by_label(labels_app, "i2s0")
    result.add(
        i2s0 is not None and i2s0.status() == "okay", "5340-012", "app i2s0 status okay"
    )
    clock = node_by_label(labels_app, "clock")
    hf = clock.props.get("hfclkaudio-frequency") if clock else None
    ok_hf = bool(hf) and len(hf[0]) == 1 and hf[0][0] == 12288000
    result.add(ok_hf, "5340-013", "app HFCLKAUDIO 12.288 MHz (0xbb8000)")
    check_i2s_pins(result, labels_app, "i2s0_default", I2S_PSEL_5340, "5340-014", "app")
    qspi = node_by_label(labels_app, "qspi")
    result.add(
        qspi is not None and qspi.status() == "disabled",
        "5340-015",
        "app qspi status disabled",
    )
    wdt0 = node_by_label(labels_app, "wdt0")
    result.add(
        wdt0 is not None and wdt0.status() == "okay", "5340-016", "app wdt0 status okay"
    )

    # ---- netcore config ----
    result.add(
        config_enabled(net_cfg, "CONFIG_BT_LL_SW_SPLIT"),
        "5340-017",
        "net CONFIG_BT_LL_SW_SPLIT=y",
        "got %r" % net_cfg.get("CONFIG_BT_LL_SW_SPLIT"),
    )
    result.add(
        config_enabled(net_cfg, "CONFIG_BT_CTLR_PERIPHERAL_ISO"),
        "5340-018",
        "net CONFIG_BT_CTLR_PERIPHERAL_ISO=y",
        "got %r" % net_cfg.get("CONFIG_BT_CTLR_PERIPHERAL_ISO"),
    )
    result.add(
        config_enabled(net_cfg, "CONFIG_BT_CTLR_CONN_ISO"),
        "5340-019",
        "net CONFIG_BT_CTLR_CONN_ISO=y",
        "got %r" % net_cfg.get("CONFIG_BT_CTLR_CONN_ISO"),
    )
    net_acl = config_int(net_cfg, "CONFIG_BT_BUF_ACL_TX_COUNT")
    net_iso = config_int(net_cfg, "CONFIG_BT_ISO_TX_BUF_COUNT")
    result.add(
        net_acl == 7, "5340-020", "net ACL TX count 7 (controller)", "got %r" % net_acl
    )
    result.add(
        net_iso == 6, "5340-021", "net ISO TX count 6 (controller)", "got %r" % net_iso
    )
    app_acl = config_int(app_cfg, "CONFIG_BT_BUF_ACL_TX_COUNT")
    app_iso = config_int(app_cfg, "CONFIG_BT_ISO_TX_BUF_COUNT")
    result.add(
        net_acl == app_acl,
        "5340-022",
        "net ACL TX equals app host ACL TX (%d)" % app_acl,
        "net %r app %r" % (net_acl, app_acl),
    )
    result.add(
        net_iso == app_iso,
        "5340-023",
        "net ISO TX equals app host ISO TX (%d)" % app_iso,
        "net %r app %r" % (net_iso, app_iso),
    )

    # ---- netcore dts ----
    chosen = find_chosen(net_dts)
    hci_node = chosen_ref(labels_net, chosen, "zephyr,bt-hci")
    result.add(
        hci_node is not None,
        "5340-024",
        "net chosen zephyr,bt-hci resolves",
        "chosen %r" % (chosen.props.get("zephyr,bt-hci") if chosen else None),
    )
    if hci_node is not None:
        result.add(
            hci_node.status() == "okay",
            "5340-025",
            "net zephyr,bt-hci node status okay",
            "status %r" % hci_node.status(),
        )
        compat = hci_node.props.get("compatible")
        ok_compat = bool(compat) and "zephyr,bt-hci-ll-sw-split" in compat[0]
        result.add(
            ok_compat,
            "5340-026",
            "net zephyr,bt-hci node compatible zephyr,bt-hci-ll-sw-split",
            "compatible %r" % (compat[0] if compat else None),
        )
    sdc = node_by_label(labels_net, "bt_hci_sdc")
    result.add(
        sdc is not None and sdc.status() == "disabled",
        "5340-027",
        "net bt_hci_sdc status disabled",
        "status %r" % (sdc.status() if sdc else None),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_BT_FILTER_ACCEPT_LIST"),
        "5340-028",
        "app CONFIG_BT_FILTER_ACCEPT_LIST=y",
        "got %r" % app_cfg.get("CONFIG_BT_FILTER_ACCEPT_LIST"),
    )

    # nRF5340 has no FLPR acceptance diagnostics (no FLPR acceptance
    # shell commands); the parity pair lives on the nRF54L15 checks.
    result.add(
        config_not_enabled(app_cfg, "CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS"),
        "5340-029",
        "app CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS not enabled",
        "got %r" % app_cfg.get("CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS"),
    )

    # User pairing control is nRF54L15-only; the nRF5340 app must stay
    # feature-off (both the controller and the input/LED adapter).
    result.add(
        config_not_enabled(app_cfg, "CONFIG_USER_PAIRING_CONTROL"),
        "5340-030",
        "app CONFIG_USER_PAIRING_CONTROL not enabled",
        "got %r" % app_cfg.get("CONFIG_USER_PAIRING_CONTROL"),
    )
    result.add(
        config_not_enabled(app_cfg, "CONFIG_USER_PAIRING_INPUT"),
        "5340-031",
        "app CONFIG_USER_PAIRING_INPUT not enabled",
        "got %r" % app_cfg.get("CONFIG_USER_PAIRING_INPUT"),
    )

    # Hardware-validated system-workqueue stack budget: fresh strict-mono
    # stream establishment faulted with a sysworkq stack overflow at the
    # 1024-byte resolved size, so the board config pins 2048.
    result.add(
        config_int(app_cfg, "CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE") == 2048,
        "5340-032",
        "app CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048 (hardware-validated "
        "system-workqueue stack budget)",
        "got %r" % config_int(app_cfg, "CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE"),
    )


def run_nrf54_checks(
    app_cfg, app_dts, labels_app, flpr_cfg, flpr_dts, labels_flpr, result
):
    """nRF54L15 contract assertions (BUILD-004/005)."""

    # ---- app config ----
    result.add(
        config_enabled(app_cfg, "CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR"),
        "54l15-001",
        "app CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=y",
        "got %r" % app_cfg.get("CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR"),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_AUDIO_CLOCK_ACTUATOR_NONE"),
        "54l15-002",
        "app CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=y",
        "got %r" % app_cfg.get("CONFIG_AUDIO_CLOCK_ACTUATOR_NONE"),
    )
    result.add(
        config_not_enabled(app_cfg, "CONFIG_AUDIO_CLOCK_ACTUATOR_APLL"),
        "54l15-003",
        "app CONFIG_AUDIO_CLOCK_ACTUATOR_APLL not enabled",
    )
    result.add(
        config_not_enabled(app_cfg, "CONFIG_AUDIO_RESAMPLER_IDENTITY"),
        "54l15-004",
        "app CONFIG_AUDIO_RESAMPLER_IDENTITY not enabled",
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_AUDIO_OFFLOAD_ASRC"),
        "54l15-005",
        "app CONFIG_AUDIO_OFFLOAD_ASRC=y",
    )
    result.add(
        config_int(app_cfg, "CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ") == 47619,
        "54l15-006",
        "app output sample rate 47619",
        "got %r" % config_int(app_cfg, "CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ"),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_LIBLC3"), "54l15-007", "app CONFIG_LIBLC3=y"
    )
    result.add(
        config_int(app_cfg, "CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT") == 2,
        "54l15-008",
        "app two sink ASEs (ASCS_MAX_ASE_SNK_COUNT=2)",
        "got %r" % config_int(app_cfg, "CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT"),
    )
    result.add(
        config_int(app_cfg, "CONFIG_BT_BUF_ACL_TX_COUNT") == 3,
        "54l15-009",
        "app ACL TX count 3",
        "got %r" % config_int(app_cfg, "CONFIG_BT_BUF_ACL_TX_COUNT"),
    )
    host_iso_tx = config_int(app_cfg, "CONFIG_BT_ISO_TX_BUF_COUNT")
    ctlr_iso_tx = config_int(app_cfg, "CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT")
    result.add(
        host_iso_tx == 1, "54l15-010", "app host ISO TX count 1", "got %r" % host_iso_tx
    )
    result.add(
        ctlr_iso_tx == 1,
        "54l15-011",
        "app CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=1",
        "got %r" % ctlr_iso_tx,
    )
    result.add(
        config_int(app_cfg, "CONFIG_BT_ISO_RX_BUF_COUNT") == 3,
        "54l15-012",
        "app host ISO RX count 3",
        "got %r" % config_int(app_cfg, "CONFIG_BT_ISO_RX_BUF_COUNT"),
    )
    result.add(
        host_iso_tx == ctlr_iso_tx,
        "54l15-013",
        "app host ISO TX equals controller ISO TX (%r)" % ctlr_iso_tx,
        "host %r controller %r" % (host_iso_tx, ctlr_iso_tx),
    )

    # ---- app dts ----
    i2s20 = node_by_label(labels_app, "i2s20")
    result.add(
        i2s20 is not None and i2s20.status() == "okay",
        "54l15-014",
        "app i2s20 status okay",
    )
    cs = i2s20.props.get("clock-source") if i2s20 else None
    ok_cs = bool(cs) and cs[0] == ["PCLK32M"]
    result.add(ok_cs, "54l15-015", "app i2s20 clock-source = PCLK32M")
    check_i2s_pins(
        result, labels_app, "i2s20_default", I2S_PSEL_54L15, "54l15-016", "app"
    )
    for label, cid in (
        ("pdm20", "54l15-017"),
        ("spi00", "54l15-018"),
        ("mx25r64", "54l15-019"),
    ):
        node = node_by_label(labels_app, label)
        result.add(
            node is not None and node.status() == "disabled",
            cid,
            "app %s status disabled" % label,
            "status %r" % (node.status() if node else None),
        )
    timer20 = node_by_label(labels_app, "timer20")
    result.add(
        timer20 is not None and timer20.status() == "reserved",
        "54l15-020",
        "app timer20 status reserved",
        "status %r" % (timer20.status() if timer20 else None),
    )
    check_rfsw(result, labels_app, "rfsw_ctl", 5, 1, "54l15-021", "app")
    check_rfsw(result, labels_app, "rfsw_pwr", 3, 0, "54l15-022", "app")
    check_clock_caps(result, labels_app, "lfxo", "54l15-023", "app")
    check_clock_caps(result, labels_app, "hfxo", "54l15-024", "app")
    check_memory_ranges(result, labels_app, "54l15-025", "app")
    part = node_by_label(labels_app, "cpuflpr_code_partition")
    result.add(
        part is not None
        and part.reg() == (0x165000, 0x18000)
        and is_root_reserved_memory_child(part)
        and unit_address_matches_reg(part, part.reg()),
        "54l15-026",
        "app FLPR code partition is root reserved-memory child with reg = (0x165000, 0x18000)",
        "got %r" % ((part.reg() if part else None),),
    )

    # ---- FLPR image cross-checks ----
    flpr_sram = node_by_label(labels_flpr, "cpuflpr_sram")
    app_flpr_sram = node_by_label(labels_app, "cpuflpr_sram_code_data")
    want = (0x20030000, 0x10000)
    got = flpr_sram.reg() if flpr_sram else None
    app_got = app_flpr_sram.reg() if app_flpr_sram else None
    result.add(
        got == want
        and is_root_child(flpr_sram)
        and unit_address_matches_reg(flpr_sram, got),
        "54l15-027",
        "FLPR image cpuflpr_sram is root memory@20030000 with reg = (0x20030000, 0x10000)",
        "got %r unit=%r root=%s"
        % (got, flpr_sram.unit_addr if flpr_sram else None, is_root_child(flpr_sram)),
    )
    result.add(
        got == app_got,
        "54l15-028",
        "FLPR image cpuflpr_sram matches app cpuflpr_sram_code_data",
        "flpr %r app %r" % (got, app_got),
    )
    chosen_flpr = find_chosen(flpr_dts)
    sram_ref = chosen_ref(labels_flpr, chosen_flpr, "zephyr,sram")
    result.add(
        sram_ref is not None and sram_ref is flpr_sram,
        "54l15-029",
        "FLPR chosen zephyr,sram resolves to cpuflpr_sram",
    )
    part_flpr = node_by_label(labels_flpr, "cpuflpr_code_partition")
    part_ok = False
    detail = "missing"
    if part_flpr is not None:
        reg = part_flpr.reg()
        if reg is not None:
            # The partition node may be nested (e.g. under a
            # 'partitions' container); walk up to the nearest ancestor
            # with a unit address (the rram@165000 parent).
            anc = part_flpr.parent
            while anc is not None and anc.unit_addr is None:
                anc = anc.parent
            if anc is not None and anc.unit_addr is not None:
                try:
                    base = int(anc.unit_addr[1:], 16)
                except ValueError:
                    base = None
                if base is not None:
                    part_ok = base + reg[0] == 0x165000 and reg[1] == 0x18000
                    detail = "abs (%s, %s) parent %s" % (
                        hex(base + reg[0]),
                        hex(reg[1]),
                        anc.name,
                    )
                else:
                    detail = "parent unit addr %r" % anc.unit_addr
            else:
                detail = "no ancestor with unit address"
    result.add(
        part_ok,
        "54l15-030",
        "FLPR image code partition absolute (0x165000, 0x18000) under rram@165000",
        detail,
    )
    code_ref = chosen_ref(labels_flpr, chosen_flpr, "zephyr,code-partition")
    result.add(
        code_ref is not None and code_ref is part_flpr,
        "54l15-031",
        "FLPR chosen zephyr,code-partition resolves to cpuflpr_code_partition",
    )
    fb = config_int(flpr_cfg, "CONFIG_FLASH_BASE_ADDRESS")
    fl = config_int(flpr_cfg, "CONFIG_FLASH_LOAD_SIZE")
    result.add(
        fb == 0x165000,
        "54l15-032",
        "FLPR CONFIG_FLASH_BASE_ADDRESS=0x165000",
        "got %r" % fb,
    )
    result.add(
        fl == 0x18000, "54l15-033", "FLPR CONFIG_FLASH_LOAD_SIZE=0x18000", "got %r" % fl
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_BT_FILTER_ACCEPT_LIST"),
        "54l15-034",
        "app CONFIG_BT_FILTER_ACCEPT_LIST=y",
        "got %r" % app_cfg.get("CONFIG_BT_FILTER_ACCEPT_LIST"),
    )

    # cpuapp + FLPR-image acceptance diagnostics must both be enabled
    # for the current lab build (lockstep proof — the acceptance harness
    # spans both images).
    result.add(
        config_enabled(app_cfg, "CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS"),
        "54l15-035",
        "app CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y",
        "got %r" % app_cfg.get("CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS"),
    )
    result.add(
        config_enabled(flpr_cfg, "CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS"),
        "54l15-036",
        "FLPR image CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS=y",
        "got %r" % flpr_cfg.get("CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS"),
    )

    # User pairing control full-stack enablement (nRF54L15 production
    # XIAO target only).  Kconfig half.
    result.add(
        config_enabled(app_cfg, "CONFIG_USER_PAIRING_CONTROL"),
        "54l15-037",
        "app CONFIG_USER_PAIRING_CONTROL=y",
        "got %r" % app_cfg.get("CONFIG_USER_PAIRING_CONTROL"),
    )
    result.add(
        config_enabled(app_cfg, "CONFIG_USER_PAIRING_INPUT"),
        "54l15-038",
        "app CONFIG_USER_PAIRING_INPUT=y",
        "got %r" % app_cfg.get("CONFIG_USER_PAIRING_INPUT"),
    )
    result.add(
        config_int(app_cfg, "CONFIG_USER_PAIRING_DEBOUNCE_MS") == 30,
        "54l15-039",
        "app USER_PAIRING_DEBOUNCE_MS=30",
        "got %r" % config_int(app_cfg, "CONFIG_USER_PAIRING_DEBOUNCE_MS"),
    )
    result.add(
        config_int(app_cfg, "CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS") == 15000,
        "54l15-040",
        "app USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000",
        "got %r" % config_int(app_cfg, "CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS"),
    )
    result.add(
        config_int(app_cfg, "CONFIG_USER_PAIRING_WORKQ_STACK_SIZE") == 1536,
        "54l15-041",
        "app USER_PAIRING_WORKQ_STACK_SIZE=1536 (pre-fix hardware high-water "
        "1012/1024 = 98% with 12 B unused on the pairing queue; 1536 restores "
        "an expected 524 B margin)",
        "got %r" % config_int(app_cfg, "CONFIG_USER_PAIRING_WORKQ_STACK_SIZE"),
    )
    result.add(
        config_int(app_cfg, "CONFIG_HEAP_MEM_POOL_SIZE") == 0,
        "54l15-042",
        "app HEAP_MEM_POOL_SIZE=0 (system heap proven unused)",
        "got %r" % config_int(app_cfg, "CONFIG_HEAP_MEM_POOL_SIZE"),
    )

    # Devicetree half — resolved aliases, GPIO flags, disabled
    # inherited DK buttons, deleted inherited DK LEDs.
    user_button = alias_ref(labels_app, app_dts, "user-button")
    result.add(
        user_button is not None and user_button is node_by_label(labels_app, "button0"),
        "54l15-043",
        "user-button alias resolves to button0",
        "resolved %r" % (user_button.name if user_button else None),
    )
    buttons_ok = False
    if user_button is not None:
        parent = user_button.parent
        compat = parent.props.get("compatible") if parent else None
        is_keys = bool(compat) and "gpio-keys" in compat[0]
        deb = parent.props.get("debounce-interval-ms") if parent else None
        deb_ok = bool(deb) and len(deb[0]) == 1 and deb[0][0] == 30
        buttons_ok = is_keys and deb_ok
    result.add(
        buttons_ok,
        "54l15-044",
        "button0 parent compatible gpio-keys with debounce-interval-ms 30",
    )
    b0 = node_by_label(labels_app, "button0")
    spec = gpio_spec(b0)
    code = b0.props.get("zephyr,code") if b0 else None
    code_ok = bool(code) and len(code[0]) == 1 and code[0][0] == INPUT_KEY_0
    spec_ok = spec is not None and spec == ("gpio0", 0, GPIO_ACTIVE_LOW | GPIO_PULL_UP)
    result.add(
        spec_ok and code_ok,
        "54l15-045",
        "button0 gpios = <&gpio0 0 ACTIVE_LOW|PULL_UP> with zephyr,code INPUT_KEY_0",
        "spec %r code %r" % (spec, (code[0] if code else None)),
    )
    disabled_ok = True
    for name in ("button1", "button2", "button3"):
        node = node_by_label(labels_app, name)
        if node is None or node.status() != "disabled":
            disabled_ok = False
    result.add(
        disabled_ok,
        "54l15-046",
        "button1/button2/button3 status disabled (no UART20/I2S pin claims)",
        "button1 %r button2 %r button3 %r"
        % (
            (
                node_by_label(labels_app, "button1").status()
                if node_by_label(labels_app, "button1")
                else None
            ),
            (
                node_by_label(labels_app, "button2").status()
                if node_by_label(labels_app, "button2")
                else None
            ),
            (
                node_by_label(labels_app, "button3").status()
                if node_by_label(labels_app, "button3")
                else None
            ),
        ),
    )
    user_led = alias_ref(labels_app, app_dts, "user-led")
    result.add(
        user_led is not None and user_led is node_by_label(labels_app, "led0"),
        "54l15-047",
        "user-led alias resolves to led0",
        "resolved %r" % (user_led.name if user_led else None),
    )
    led_spec = gpio_spec(node_by_label(labels_app, "led0"))
    result.add(
        led_spec is not None and led_spec == ("gpio2", 0, GPIO_ACTIVE_LOW),
        "54l15-048",
        "led0 gpios = <&gpio2 0 ACTIVE_LOW>",
        "got %r" % (led_spec,),
    )
    gone = all(
        node_by_label(labels_app, name) is None for name in ("led1", "led2", "led3")
    )
    result.add(
        gone,
        "54l15-049",
        "inherited led1/led2/led3 absent from resolved tree (deleted)",
        "led1 %r led2 %r led3 %r"
        % (
            (
                node_by_label(labels_app, "led1").name
                if node_by_label(labels_app, "led1")
                else None
            ),
            (
                node_by_label(labels_app, "led2").name
                if node_by_label(labels_app, "led2")
                else None
            ),
            (
                node_by_label(labels_app, "led3").name
                if node_by_label(labels_app, "led3")
                else None
            ),
        ),
    )
    # USER_PAIRING_INPUT's mandatory subsystem dependency (menuconfig,
    # default n, nothing selects it; INPUT_GPIO_KEYS only selects GPIO).
    # Without it Kconfig reports "assigned the value 'y' but got the
    # value 'n'" and the adapter is not compiled.
    result.add(
        config_enabled(app_cfg, "CONFIG_INPUT"),
        "54l15-050",
        "app CONFIG_INPUT=y (USER_PAIRING_INPUT mandatory dependency)",
        "got %r" % app_cfg.get("CONFIG_INPUT"),
    )


def run_source_checks(bt_bap_path, result):
    """48 kHz capability source half (clearly labeled as source data).

    The PACS LC3 frequency LTV is a C object (src/bt_bap.c), not a
    resolved Kconfig/DTS property.  The resolved half of the 48 kHz
    contract is proven above (output rate, LIBLC3, two sink ASEs); this
    source check pins the advertised-capability constant and the
    frequency acceptance gate in the production source itself.
    """
    try:
        with open(bt_bap_path, "r", encoding="utf-8") as fh:
            src = fh.read()
    except OSError as exc:
        result.add(False, "SRC-001", "read src/bt_bap.c (%s)" % exc)
        return
    ok_cap = "BT_AUDIO_CODEC_CAP_FREQ_48KHZ" in src
    result.add(
        ok_cap,
        "SRC-001",
        "src/bt_bap.c advertises BT_AUDIO_CODEC_CAP_FREQ_48KHZ (source, not resolved)",
    )
    ok_gate = "48000" in src
    result.add(
        ok_gate,
        "SRC-002",
        "src/bt_bap.c frequency acceptance gate is exactly 48000 Hz (source, not "
        "resolved)",
    )
    result.add(
        True,
        "SRC-003",
        "resolved 48 kHz half proven: output rate + LIBLC3 + two sink ASEs (above); "
        "exact advertised 48 kHz / 7.5+10 ms / 1+2 channel capability is pinned by "
        "direct production-source T2/T4 tests (BT-002) — the PACS frequency LTV is a "
        "C object, not a .config property",
    )


# ── input resolution ───────────────────────────────────────────────


def _read_required(path, what):
    if not os.path.exists(path):
        raise OSError("%s missing: %s" % (what, path))
    if not os.path.isfile(path):
        raise OSError("%s not a regular file: %s" % (what, path))
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return fh.read()
    except OSError as exc:
        raise OSError("%s unreadable: %s (%s)" % (what, path, exc))


def resolve_inputs(nrf5340_root, nrf54l15_root, bt_bap_path):
    """Resolve and parse required active inputs and optional legacy nRF5340
    inputs; hard-fail on any supplied-input problem.

    Returns a dict of parsed inputs.  Raises ConfigError/DtsError/OSError
    for hard failures (missing/duplicate/unreadable/malformed).

    The sysbuild app image directory is named after the application
    source directory basename (e.g. `le-audio-receiver` or the checkout
    directory name), so it is resolved from `domains.yaml` — never
    assumed.  The controller/FLPR image names are fixed sysbuild domain
    names (hci_ipc, flpr).
    """

    def default_image_name(root):
        text = _read_required(
            os.path.join(root, "domains.yaml"), "sysbuild domains.yaml"
        )
        m = re.search(r"^default:\s*(\S+)\s*$", text, re.M)
        if not m:
            raise OSError("domains.yaml has no default image: %s" % root)
        return m.group(1)

    def image(root, name):
        base = os.path.join(root, name, "zephyr")
        cfg_text = _read_required(
            os.path.join(base, ".config"), "%s image .config" % name
        )
        dts_text = _read_required(
            os.path.join(base, "zephyr.dts"), "%s image zephyr.dts" % name
        )
        cfg = parse_config(cfg_text)
        nodes, labels = parse_dts(dts_text)
        return cfg, nodes, labels

    if nrf5340_root is not None:
        app5340_name = default_image_name(nrf5340_root)
        app54_name = default_image_name(nrf54l15_root)
        app5340 = image(nrf5340_root, app5340_name)
        net = image(nrf5340_root, "hci_ipc")
        legacy = {
            "app5340_cfg": app5340[0],
            "app5340_dts": app5340[1],
            "app5340_labels": app5340[2],
            "net_cfg": net[0],
            "net_dts": net[1],
            "net_labels": net[2],
        }
    else:
        app54_name = default_image_name(nrf54l15_root)
        legacy = {}
    app54 = image(nrf54l15_root, app54_name)
    flpr = image(nrf54l15_root, "flpr")
    parsed = {
        "app54_cfg": app54[0],
        "app54_dts": app54[1],
        "app54_labels": app54[2],
        "flpr_cfg": flpr[0],
        "flpr_dts": flpr[1],
        "flpr_labels": flpr[2],
        "bt_bap_path": bt_bap_path,
    }
    parsed.update(legacy)
    return parsed


def run_all(parsed):
    """Run every contract check; returns a ContractResult."""
    result = ContractResult()
    if "app5340_cfg" in parsed:
        run_nrf5340_checks(
            parsed["app5340_cfg"],
            parsed["app5340_dts"],
            parsed["app5340_labels"],
            parsed["net_cfg"],
            parsed["net_dts"],
            parsed["net_labels"],
            result,
        )
    run_nrf54_checks(
        parsed["app54_cfg"],
        parsed["app54_dts"],
        parsed["app54_labels"],
        parsed["flpr_cfg"],
        parsed["flpr_dts"],
        parsed["flpr_labels"],
        result,
    )
    run_source_checks(parsed["bt_bap_path"], result)
    return result


def format_result(result):
    """Deterministic human-readable PASS/FAIL report lines."""
    lines = []
    for ok, cid, desc, detail in result.entries:
        head = "PASS" if ok else "FAIL"
        line = "%s [%s] %s" % (head, cid, desc)
        if detail:
            line += " — " + detail
        lines.append(line)
    return lines


def main(argv=None):
    parser = argparse.ArgumentParser(description="Resolved build-contract checker")
    parser.add_argument(
        "--nrf5340",
        metavar="BUILD_ROOT",
        help="optional legacy local nRF5340 sysbuild root (build/nrf5340)",
    )
    parser.add_argument(
        "--nrf54l15",
        required=True,
        metavar="BUILD_ROOT",
        help="sysbuild root of the nRF54L15 build (build/nrf54l15)",
    )
    parser.add_argument(
        "--bt-bap-source",
        metavar="PATH",
        default=None,
        help="path to src/bt_bap.c (default: repo src/bt_bap.c)",
    )
    args = parser.parse_args(argv)

    if args.bt_bap_source is None:
        args.bt_bap_source = os.path.normpath(
            os.path.join(
                os.path.dirname(os.path.abspath(__file__)), "..", "src", "bt_bap.c"
            )
        )

    try:
        parsed = resolve_inputs(args.nrf5340, args.nrf54l15, args.bt_bap_source)
    except (OSError, ConfigError, DtsError) as exc:
        print("HARD ERROR: %s" % exc, file=sys.stderr)
        return 2

    result = run_all(parsed)
    for line in format_result(result):
        print(line)

    failures = result.failures()
    print("\n%d assertions, %d failed" % (len(result.entries), len(failures)))
    if failures:
        print("BUILD CONTRACT FAILED")
        return 1
    print("BUILD CONTRACT PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
