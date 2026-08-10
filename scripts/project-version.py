#!/usr/bin/env python3
"""Stdlib-only reader for the Zephyr root application ``VERSION`` file.

Prints the canonical public semantic version ``MAJOR.MINOR.PATCH`` that the
FR1 packager accepts (``docs/development/firmware-release-plan.md`` FR1).

Public CLI:

    python3 scripts/project-version.py
    python3 scripts/project-version.py --version-file /path/to/VERSION

Behavior:

- default file is the repository-root ``VERSION``, resolved relative to this
  script's fixed location, not the caller CWD;
- explicit ``--version-file`` exists for tests and future tooling;
- reads a regular, non-symlink, nonempty UTF-8 file;
- accepts the five Zephyr keys exactly once; blank lines and ``#`` comments
  are allowed, unknown non-comment lines, duplicate keys, missing keys, and
  malformed assignments are rejected;
- numeric values must be canonical unsigned decimal (no leading zeros except
  a literal ``0``) and fit Zephyr's 0..255 range;
- requires ``VERSION_TWEAK = 0`` and empty ``EXTRAVERSION`` because public
  artifact versions use the FR1 canonical ``MAJOR.MINOR.PATCH`` contract;
- prints exactly one canonical ``MAJOR.MINOR.PATCH`` line plus newline and
  nothing to stderr;
- caller/file errors print one ``project-version: error: <reason>`` line,
  no traceback, and return nonzero;
- no environment, Git, timestamps, host paths, or NCS state affect output.
"""

import argparse
import os
import re
import stat
import sys

KEYS = ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL", "VERSION_TWEAK", "EXTRAVERSION")
ERROR_PREFIX = "project-version: error: "
_NUMERIC_RE = re.compile(r"^(0|[1-9][0-9]*)$")
_ASSIGN_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


class VersionError(Exception):
    """A caller/file error reported as a clean stderr diagnostic."""


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _read_version_file(path):
    """Validate and read one version file as decoded text."""
    if os.path.islink(path):
        raise VersionError("%s is a symlink" % path)
    try:
        st = os.stat(path)
    except OSError as exc:
        raise VersionError("%s cannot be read: %s" % (path, exc))

    if not stat.S_ISREG(st.st_mode):
        raise VersionError("%s is not a regular file" % path)
    if st.st_size == 0:
        raise VersionError("%s is empty" % path)
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
        return raw.decode("utf-8")
    except OSError as exc:
        raise VersionError("%s cannot be read: %s" % (path, exc))
    except UnicodeDecodeError as exc:
        raise VersionError("%s is not valid UTF-8: %s" % (path, exc))


def parse_version(text):
    """Parse five-field Zephyr ``VERSION`` text into (major, minor, patch).

    Returns the canonical public semantic version as a string.  Raises
    ``VersionError`` on any structural or value violation.
    """
    values = {}
    for lineno, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        match = _ASSIGN_RE.match(line)
        if not match:
            raise VersionError("malformed assignment on line %d: %r" % (lineno, raw))
        key, value = match.group(1), match.group(2)
        if key not in KEYS:
            raise VersionError("unknown key %r on line %d" % (key, lineno))
        if key in values:
            raise VersionError("duplicate key %s on line %d" % (key, lineno))
        values[key] = value

    missing = [key for key in KEYS if key not in values]
    if missing:
        raise VersionError("missing required key(s): %s" % ", ".join(missing))

    for key in KEYS:
        value = values[key]
        if key == "EXTRAVERSION":
            continue
        if not _NUMERIC_RE.match(value):
            raise VersionError(
                "invalid %s value %r (expected canonical unsigned decimal, "
                "no leading zeros)" % (key, value)
            )
        number = int(value)
        if number > 255:
            raise VersionError(
                "%s value %d is outside Zephyr's 0..255 range" % (key, number)
            )

    if values["VERSION_TWEAK"] != "0":
        raise VersionError(
            "VERSION_TWEAK must be 0 for the FR1 canonical MAJOR.MINOR.PATCH "
            "contract (found %r)" % values["VERSION_TWEAK"]
        )
    if values["EXTRAVERSION"] != "":
        raise VersionError(
            "EXTRAVERSION must be empty for the FR1 canonical MAJOR.MINOR.PATCH "
            "contract (found %r)" % values["EXTRAVERSION"]
        )

    return "%s.%s.%s" % (
        values["VERSION_MAJOR"],
        values["VERSION_MINOR"],
        values["PATCHLEVEL"],
    )


class _ArgumentParser(argparse.ArgumentParser):
    """Argparse variant that emits exactly one clean stderr diagnostic
    without a usage block or traceback."""

    def error(self, message):
        self.exit(2, "%s%s\n" % (ERROR_PREFIX, message))


def _build_parser():
    parser = _ArgumentParser(prog="project-version", add_help=True)
    parser.add_argument(
        "--version-file",
        default=None,
        help="path to the Zephyr VERSION file (default: repository-root VERSION)",
    )
    return parser


def main(argv=None):
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        path = (
            args.version_file
            if args.version_file
            else os.path.join(_repo_root(), "VERSION")
        )
        text = _read_version_file(path)
        print(parse_version(text))
    except VersionError as exc:
        print("%s%s" % (ERROR_PREFIX, exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
