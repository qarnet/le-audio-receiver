#!/usr/bin/env python3
"""Recognize exact CMake capability-probe diagnostics in a configure YAML.

The installed Zephyr linker flags (`cmake/linker/ld/linker_flags.cmake`
lines 10, 18, 22) purposefully capability-test options whose failed probe
links print diagnostics (`-static`, `-Wl,-N`, orphan warn/error).  The
runner's strict build-warning grammar must not treat those recorded probe
logs as real build diagnostics; but the disposition is exact, bounded and
source-verified, never a blanket ignore.

`inspect_configure_probes(configure_text, sdk)` parses the YAML with a
limited event-envelope split (exact top-level `-` separator lines), keeps
every byte of the input unchanged, and returns structured recognized
probe records; any diagnostic-bearing event outside the recognized set
(or any unrecognized shape inside one) raises ValueError with the probe
and line number.  Callers must have verified the installed SDK sources
(`verify_sdk_sources`) before granting any disposition.
"""

import hashlib
import os
import re
import stat
from collections import Counter
from pathlib import Path


# Installed SDK support files whose bytes bind the dispositions.
SUPPORTED_SOURCES = {
    "linker_flags": "zephyr/cmake/linker/ld/linker_flags.cmake",
    "extensions": "zephyr/cmake/modules/extensions.cmake",
}

EXPECTED_SDK_SHA256 = {
    "zephyr/cmake/linker/ld/linker_flags.cmake": "c476ef56c83deb6553a852218fd70fd921aa587a51d66d31082c234159735a26",
    "zephyr/cmake/modules/extensions.cmake": "6cacb57208f801f9062eac5b75a411d1be0ed7de2a250843ae0334dd3a94a9f3",
}

DIAGNOSTIC = re.compile(
    r"(?i)(?:\b(?:fatal\s+error|warning|error):|"
    r"\bCMake\s+(?:Warning|Error)\b|"
    r"\b[A-Za-z]+Warning:|\bskipping incompatible\b|"
    r"\bninja:\s+build stopped\b|\bFAILED:|\bFATAL(?: ERROR)?:)"
)

GCC_DIR = "/nix/store/iwf80230xr0z8pqh1jk3z8rgw67ydagm-gcc-wrapper-14.3.0/bin/gcc"
BINUTILS_LD = "/nix/store/i7mdvmliqcb5lz0nqija8rq55vws6gi8-binutils-2.44/bin/ld.bfd"
GLIBC_CRT_DIR = (
    "/nix/store/yhawd8dka2563b5mg3vjm5h14sw5lv95-glibc-multi-2.40-224/lib/32"
)
GCC_CRT_DIR = "/nix/store/5qymnlw49666rc5b15c1pz9ryd8qidna-gcc-14.3.0/lib/gcc/x86_64-unknown-linux-gnu/14.3.0/32"

EVENT_HEAD = re.compile(r"^  -\s*$")

# Exact required backtrace suffixes: absolute machine prefix plus the
# named pinned CMake-internal / SDK source path fragment.
TOOLCHAIN_CMAKE_PREFIX = "/toolchains/8285d8ad56/usr/local/share/cmake-4.2/Modules"
SDK_EXTENSIONS_PREFIX = "/ncs/v3.4.1/zephyr/cmake/modules"
SDK_LINKER_LD_PREFIX = "/ncs/v3.4.1/zephyr/cmake/linker/ld"
REQUIRED_BACKTRACE = (
    TOOLCHAIN_CMAKE_PREFIX + "/Internal/CheckSourceCompiles.cmake:104 (try_compile)",
    TOOLCHAIN_CMAKE_PREFIX
    + "/Internal/CheckCompilerFlag.cmake:18 (cmake_check_source_compiles)",
    TOOLCHAIN_CMAKE_PREFIX
    + "/CheckCCompilerFlag.cmake:105 (cmake_check_compiler_flag)",
    SDK_EXTENSIONS_PREFIX + "/extensions.cmake:2421 (check_c_compiler_flag)",
    SDK_EXTENSIONS_PREFIX + "/extensions.cmake:1199 (check_compiler_flag)",
    SDK_EXTENSIONS_PREFIX + "/extensions.cmake:2604 (zephyr_check_compiler_flag)",
)
LINKER_FLAGS_LINE = {
    "-static": "linker_flags.cmake:10",
    "-Wl,-N": "linker_flags.cmake:10",
    "-Wl,--orphan-handling=warn": "linker_flags.cmake:18",
    "-Wl,--orphan-handling=error": "linker_flags.cmake:22",
}
ORPHAN_PAIRS = (
    (".note.gnu.property", "Scrt1.o"),
    (".rel.fini_array", "Scrt1.o"),
    (".rel.init_array", "Scrt1.o"),
    (".tm_clone_table", "crtbeginS.o"),
    (".tm_clone_table", "crtendS.o"),
)
ORPHAN_WARN_PLACEMENTS = (
    ".note.gnu.property",
    ".rel.dyn",
    ".rel.dyn",
    ".tm_clone_table",
    ".tm_clone_table",
)


def verify_sdk_sources(sdk_root):
    """Check the exact installed SDK support files before dispositions.

    One bounded regular non-symlink read per file via O_NOFOLLOW/O_NONBLOCK
    and fstat; the digest comes from that single raw snapshot.
    """
    result = {}
    for rel, expected in EXPECTED_SDK_SHA256.items():
        path = sdk_root / rel
        try:
            fd = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except OSError as exc:
            raise ValueError(f"SDK capability source {rel} unreadable: {exc}")
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode):
                raise ValueError(f"SDK capability source {rel} not regular")
            if info.st_size > 2 * 1024 * 1024:
                raise ValueError(f"SDK capability source {rel} oversized")
            with os.fdopen(fd, "rb") as fh:
                fd = None
                raw = fh.read(2 * 1024 * 1024 + 1)
        finally:
            if fd is not None:
                os.close(fd)
        digest = hashlib.sha256(raw).hexdigest()
        if digest != expected:
            raise ValueError(
                f"SDK capability source {rel} drifted: {digest} vs {expected}"
            )
        result[rel] = digest
    return result


def configure_text_meta(probe, exit_match):
    exit_value = exit_match.group(1) if exit_match else "unknown"
    return f"probe={probe} exit={exit_value}"


def _split_events(text):
    lines = text.splitlines()
    heads = [i for i, line in enumerate(lines) if EVENT_HEAD.fullmatch(line)]
    if not heads:
        return None
    for index, line in enumerate(lines[: heads[0]]):
        if DIAGNOSTIC.search(line):
            raise ValueError(f"configure prefix line={index + 1}: {line.strip()}")
    stops = heads[1:] + [len(lines)]
    return [
        ("\n".join(lines[start + 1 : stop]), start + 2)
        for start, stop in zip(heads, stops)
    ]


def _collect_stdout(lines):
    out = []
    for line in lines:
        if line.startswith("        "):
            content = line[8:]
            if content.strip():
                out.append(content)
            # Blank separator lines inside the stdout block continue.
            continue
        if line.startswith("      "):
            break
    return "\n".join(out)


def inspect_configure_probes(configure_text, sdk_root):
    """Recognized probe records; unknown diagnostics raise ValueError.

    configure_text is the raw bounded YAML text; sdk_root pins the exact
    installed NCS root.  Returns a list of structured probe records.
    """
    recognized = []
    events = _split_events(configure_text)
    if events is None:
        # No envelope at all: any diagnostic is an unrecognized standalone
        # configure log shape (e.g. the old synthetic warning fixtures);
        # reject with the exact line and the enclosing variable if present.
        known_variables = re.findall(r'variable: "([^"\n]+)"', configure_text)
        for number, line in enumerate(configure_text.splitlines(), 1):
            if DIAGNOSTIC.search(line):
                probe = (
                    known_variables[-1]
                    if known_variables
                    else (
                        "CMakeCCompilerId"
                        if "CMakeCCompilerId" in configure_text
                        else "compiler-id/unknown"
                    )
                )
                exit_match = re.search(r"exitCode:\s*(-?\d+)", configure_text)
                raise ValueError(
                    f"{configure_text_meta(probe, exit_match)} line={number}: "
                    f"{line.strip()}"
                )
        return []
    seen_variables = set()
    for event_text, lineno in events:
        has_diagnostic = any(
            DIAGNOSTIC.search(line) for line in event_text.splitlines()
        )
        if not has_diagnostic:
            # No diagnostics: not a recognized probe; nothing to exempt.
            continue
        record = _recognize_diagnostic(event_text, lineno, sdk_root)
        _require(
            record["variable"] not in seen_variables,
            f"duplicate recognized probe {record['variable']!r} (line {lineno})",
            lineno,
            record["variable"],
        )
        seen_variables.add(record["variable"])
        recognized.append(record)
    return recognized


def _require(cond, message, lineno, probe=None):
    if not cond:
        raise ValueError(f"{message} (probe={probe!r} line={lineno})")


def _field_once(event_text, prefix, pattern, label, lineno):
    rows = re.findall(r"^" + re.escape(prefix) + r".*$", event_text, re.M)
    _require(len(rows) == 1, f"probe field {label} must occur once", lineno)
    match = re.fullmatch(pattern, rows[0])
    _require(match is not None, f"probe field {label} malformed", lineno)
    return match


def _recognize_diagnostic(event_text, lineno, sdk_root):
    kind_match = _field_once(
        event_text,
        "    kind:",
        r'^    kind: "([^"]+)"$',
        "kind",
        lineno,
    )
    kind = kind_match.group(1)
    _require(
        kind == "try_compile-v1",
        f"unknown diagnostic event kind {kind!r}",
        lineno,
    )
    backtrace = re.findall(r'^      - "([^"\n]+)"$', event_text, re.M)
    for required in REQUIRED_BACKTRACE:
        pattern = r"^/.*" + re.escape(required) + r"$"
        matches = [entry for entry in backtrace if re.fullmatch(pattern, entry)]
        _require(
            len(matches) == 1,
            f"missing required backtrace entry {required!r}",
            lineno,
        )
    variable = _field_once(
        event_text,
        "      variable:",
        r'^      variable: "([^"\n]+)"$',
        "variable",
        lineno,
    )
    name = variable.group(1)

    options = {
        "check_C__fuse_ld_bfd__static": "-static",
        "check_C__fuse_ld_bfd__Wl__N": "-Wl,-N",
        "check_C__fuse_ld_bfd__Wl___orphan_handling_warn": "-Wl,--orphan-handling=warn",
        "check_C__fuse_ld_bfd__Wl___orphan_handling_error": (
            "-Wl,--orphan-handling=error"
        ),
    }
    _require(name in options, f"unknown probe variable {name!r}", lineno, name)
    option = options[name]

    flags_pattern = (
        r"^/.*"
        + re.escape(
            SDK_LINKER_LD_PREFIX
            + "/"
            + LINKER_FLAGS_LINE[option]
            + " (check_set_linker_property)"
        )
        + r"$"
    )
    flags_matches = [entry for entry in backtrace if re.fullmatch(flags_pattern, entry)]
    _require(
        len(flags_matches) == 1,
        f"probe {name} backtrace linker_flags line mismatch",
        lineno,
        name,
    )
    cached = _field_once(
        event_text,
        "      cached:",
        r"^      cached: (true|false)$",
        "cached",
        lineno,
    )
    _require(
        cached.group(1) == "true",
        "probe event missing cached:true",
        lineno,
        name,
    )
    exit_match = _field_once(
        event_text,
        "      exitCode:",
        r"^      exitCode: (\d+)$",
        "exitCode",
        lineno,
    )
    exit_code = int(exit_match.group(1))
    if option.endswith("warn"):
        _require(
            exit_code == 0,
            f"probe {name} must succeed (got {exit_code})",
            lineno,
            name,
        )
    else:
        _require(
            exit_code == 1,
            f"probe {name} must fail (got {exit_code})",
            lineno,
            name,
        )

    flags = _field_once(
        event_text,
        "      CMAKE_C_FLAGS:",
        r'^      CMAKE_C_FLAGS: "(.*?)"$',
        "CMAKE_C_FLAGS",
        lineno,
    )
    _require(
        flags.group(1) == "-m32",
        f"probe {name} CMAKE_C_FLAGS must be exactly -m32",
        lineno,
        name,
    )
    exe_flags = _field_once(
        event_text,
        "      CMAKE_EXE_LINKER_FLAGS:",
        r'^      CMAKE_EXE_LINKER_FLAGS: "(.*?)"$',
        "CMAKE_EXE_LINKER_FLAGS",
        lineno,
    )
    _require(
        exe_flags.group(1) == "",
        f"probe {name} CMAKE_EXE_LINKER_FLAGS must be empty",
        lineno,
        name,
    )

    stdout_match = _field_once(
        event_text,
        "      stdout:",
        r"^      stdout: \|$",
        "stdout",
        lineno,
    )
    event_lines = event_text.splitlines()
    stdout_index = next(
        index for index, line in enumerate(event_lines) if line == "      stdout: |"
    )
    std = _collect_stdout(event_lines[stdout_index + 1 :])

    _require(
        "cannot find entry symbol _start" not in std,
        f"probe {name} must not carry the old _start message",
        lineno,
        name,
    )

    # Exact argv validation: compile count 1 with [1/2] prefix, the exact
    # cmTC_[hex] id and the absolute scratch path; the same object id for
    # the link; the exact option and --entry=main on every link row; extra
    # unrecognized compiler/link rows inside the event reject.
    gcc = GCC_DIR
    compile_line_pattern = (
        r"^\[1/2\] "
        + re.escape(gcc)
        + r" -D"
        + re.escape(name)
        + r"  -m32 -fuse-ld=bfd "
        + re.escape(option)
        + r" -o CMakeFiles/(cmTC_[a-f0-9]+)\.dir/src\.c\.obj -c "
        r"/\S+/CMakeScratch/TryCompile-[a-zA-Z0-9]+/src\.c$"
    )
    compile_matches = list(re.finditer(compile_line_pattern, std, re.M))
    _require(
        len(compile_matches) == 1,
        f"probe {name} compile argv invalid",
        lineno,
        name,
    )
    compile_line = compile_matches[0]
    cm_id = compile_line.group(1)
    link_line_pattern = (
        r"^(?:\[2/2\] )?(?:: && )?"
        + re.escape(gcc)
        + r" -m32 -fuse-ld=bfd "
        + re.escape(option)
        + " -Wl,--entry=main CMakeFiles/"
        + re.escape(cm_id)
        + r"\.dir/src\.c\.obj -o "
        + re.escape(cm_id)
        + r"\s+&& :$"
    )
    link_lines = re.findall(link_line_pattern, std, re.M)
    expected_links = 2 if exit_code == 1 else 1
    _require(
        len(link_lines) == expected_links,
        f"probe {name} link argv count {len(link_lines)} != {expected_links}",
        lineno,
        name,
    )
    # Extra recognized-but-unlisted compiler/link rows reject: any row
    # mentioning the compiler or an exact cmTC id beyond the expected
    # compile/link/echo pattern above.
    all_gcc_rows = re.findall(r"\S*gcc-wrapper[^ ]*gcc", std)
    expected_compile_count = 1
    _require(
        len(all_gcc_rows) >= expected_compile_count,
        f"probe {name} compiler row missing",
        lineno,
        name,
    )
    # Exact expected gcc row instances: 1 compile + expected_links.
    expected_gcc_rows = 1 + expected_links
    _require(
        len(all_gcc_rows) == expected_gcc_rows,
        f"probe {name} compiler rows {len(all_gcc_rows)} != {expected_gcc_rows}",
        lineno,
        name,
    )

    # Exact recognized diagnostic rows: the whole stdout block's ld.bfd
    # / FAILED / collect2 / ninja rows must match the expected multiset
    # exactly (Counter), with no extra generic warning/error.
    actual = Counter(
        line.strip()
        for line in event_text.splitlines()
        if DIAGNOSTIC.search(line)
        or line.strip().startswith(BINUTILS_LD + ": ")
        or line.strip().startswith(": && " + gcc)
    )
    if option == "-static":
        expected_actual = Counter(
            [
                BINUTILS_LD + ": cannot find -lc: No such file or directory",
                BINUTILS_LD
                + ": have you installed the static version of the c library ?",
                "FAILED: [code=1] " + cm_id,
                ": && "
                + gcc
                + " -m32 -fuse-ld=bfd -static -Wl,--entry=main CMakeFiles/"
                + cm_id
                + ".dir/src.c.obj -o "
                + cm_id
                + "   && :",
                "collect2: error: ld returned 1 exit status",
                "ninja: build stopped: subcommand failed.",
            ]
        )
    elif option == "-Wl,-N":
        expected_actual = Counter(
            [
                BINUTILS_LD + ": cannot find -lgcc_s: No such file or directory",
                BINUTILS_LD
                + ": have you installed the static version of the gcc_s library ?",
                BINUTILS_LD + ": cannot find -lgcc_s: No such file or directory",
                BINUTILS_LD
                + ": have you installed the static version of the gcc_s library ?",
                BINUTILS_LD + ": cannot find -lc: No such file or directory",
                BINUTILS_LD
                + ": have you installed the static version of the c library ?",
                "FAILED: [code=1] " + cm_id,
                ": && "
                + gcc
                + " -m32 -fuse-ld=bfd -Wl,-N -Wl,--entry=main CMakeFiles/"
                + cm_id
                + ".dir/src.c.obj -o "
                + cm_id
                + "   && :",
                "collect2: error: ld returned 1 exit status",
                "ninja: build stopped: subcommand failed.",
            ]
        )
    else:
        # Per-row shape: (section, object-base, source-path, placement).
        warns = list(
            zip(
                ORPHAN_PAIRS,
                ORPHAN_WARN_PLACEMENTS,
            )
        )
        rows_described = []
        for (section, object_base), placement in warns:
            source_path = (
                (GLIBC_CRT_DIR + "/Scrt1.o")
                if object_base == "Scrt1.o"
                else (
                    (GCC_CRT_DIR + "/crtbeginS.o")
                    if "begin" in object_base
                    else (GCC_CRT_DIR + "/crtendS.o")
                )
            )
            rows_described.append((section, object_base, source_path, placement))
        kind_word = "warning" if option.endswith("warn") else "error"
        expected_actual = Counter()
        for section, object_base, source_object_path, placement in rows_described:
            if option.endswith("warn"):
                row = (
                    BINUTILS_LD
                    + ": "
                    + kind_word
                    + ": orphan section `"
                    + section
                    + "' from `"
                    + source_object_path
                    + "' being placed in section `"
                    + placement
                    + "'"
                )
            else:
                row = (
                    BINUTILS_LD
                    + ": "
                    + kind_word
                    + ": unplaced orphan section `"
                    + section
                    + "' from `"
                    + source_object_path
                    + "'"
                )
            expected_actual[row] += 1
        if option.endswith("error"):
            expected_actual.update(
                {
                    "FAILED: [code=1] " + cm_id: 1,
                    ": && "
                    + gcc
                    + " -m32 -fuse-ld=bfd -Wl,--orphan-handling=error -Wl,--entry=main CMakeFiles/"
                    + cm_id
                    + ".dir/src.c.obj -o "
                    + cm_id
                    + "   && :": 1,
                    "collect2: error: ld returned 1 exit status": 1,
                    "ninja: build stopped: subcommand failed.": 1,
                }
            )
    _require(
        actual == expected_actual,
        f"probe {name} diagnostic multiset wrong: extra={dict(actual - expected_actual)} missing={dict(expected_actual - actual)}",
        lineno,
        name,
    )
    supported = exit_code == 0
    sdk_hashes = verify_sdk_sources(sdk_root)
    return {
        "configure_line": lineno,
        "kind": "try_compile-v1",
        "variable": name,
        "option": option,
        "exit_code": exit_code,
        "supported": supported,
        "disposition": linker_flags_option_disposition(option),
        "sdk_hashes": sdk_hashes,
        "cm_id": cm_id,
        "link_command_count": len(link_lines),
        "orphan_pairs": (
            orphan_pairs_from(std) if "--orphan-handling" in option else []
        ),
    }


def linker_flags_option_disposition(option):
    return {
        "-static": "capability-test-failed-expected-missing-static-libc",
        "-Wl,-N": "capability-test-failed-expected-missing-static-gcc_s",
        "-Wl,--orphan-handling=warn": "capability-test-warn-supported",
        "-Wl,--orphan-handling=error": "capability-test-failed-expected-orphan-error",
    }[option]


def orphan_pairs_from(std):
    result = []
    for m in re.finditer(r"orphan section `([^']+)' from `([^']+)'", std):
        section, source = m.group(1), m.group(2)
        base = source.rsplit("/", 1)[-1]
        result.append((section, base))
    return result
