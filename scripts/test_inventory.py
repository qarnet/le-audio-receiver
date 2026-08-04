#!/usr/bin/env python3
"""Deterministic test-suite inventory for le-audio-receiver (stdlib only).

Sole filesystem classification source for the canonical gate, the coverage
runner, and the test-matrix checker.  Adding a suite anywhere under the
inventoried roots is automatically picked up by every consumer; there is no
separate manifest to keep in sync.

Classification (deterministic sorted output, repo-relative paths):

  - Twister C suites: immediate ``tests/unit/*/`` directories containing a
    ``testcase.yaml`` file.
  - Exec-only C suites: immediate ``tests/unit/*/`` directories containing a
    ``CMakeLists.txt`` but **no** ``testcase.yaml``.  Helper directories
    without a ``CMakeLists.txt`` are not suites.
  - Python children: immediate ``tests/unit/*/test_*.py`` files inside
    directories with neither ``CMakeLists.txt`` nor ``testcase.yaml``, plus
    immediate ``scripts/test_*.py`` files.  Each discovered file is exactly
    one canonical gate child.

Every discovery pass validates its own output: duplicate labels or paths
raise ValueError (defensive; the same guarantee is asserted by the matrix
checker), so a misconfiguration fails loudly instead of silently changing
the gate composition.

CLI (bash-consumable):

  python3 scripts/test_inventory.py --twister      # one suite name per line
  python3 scripts/test_inventory.py --exec-only    # one suite name per line
  python3 scripts/test_inventory.py --python       # label<TAB>relpath per line
  python3 scripts/test_inventory.py --count        # total gate children
  python3 scripts/test_inventory.py --json         # full machine-readable record
  python3 scripts/test_inventory.py --repo-root X  # override repo root

Importable API:

  inv = test_inventory.discover(repo_root)
  inv.twister, inv.exec_only, inv.python_children (PyChild(label, path)),
  inv.total(), inv.to_dict()
"""

import argparse
import json
import os
import sys
from dataclasses import dataclass

UNITS_DIR = "tests/unit"
SCRIPTS_DIR = "scripts"

PYTEST_PREFIX = "test_"

# The classifier itself matches scripts/test_*.py but is a helper, not a
# gate child: it is the single documented exclusion from python-child
# discovery.  Any future scripts/test_*.py is a canonical child.
SELF_EXCLUSIONS = frozenset({"scripts/test_inventory.py"})


class InventoryError(ValueError):
    """Raised for a structurally invalid inventory (duplicate label/path)."""


@dataclass(frozen=True)
class PyChild:
    """One canonical Python gate child: a label and a repo-relative path."""

    label: str
    path: str  # repo-relative, posix separators, existing file


@dataclass(frozen=True)
class Inventory:
    """Deterministic suite inventory for one repo root."""

    twister: tuple
    exec_only: tuple
    python_children: tuple
    repo_root: str

    def total(self):
        return len(self.twister) + len(self.exec_only) + len(self.python_children)

    def all_c_suites(self):
        """All C suite names (twister first, then exec-only)."""
        return tuple(self.twister) + tuple(self.exec_only)

    def to_dict(self):
        return {
            "repo_root": self.repo_root,
            "twister": list(self.twister),
            "exec_only": list(self.exec_only),
            "python_children": [
                {"label": c.label, "path": c.path} for c in self.python_children
            ],
            "total": self.total(),
        }


def _posix(path):
    return path.replace(os.sep, "/")


def _immediate_subdirs(root, parent):
    """Sorted immediate subdirectory names of parent that exist on disk."""
    full = os.path.join(root, parent)
    if not os.path.isdir(full):
        return []
    out = []
    for name in sorted(os.listdir(full)):
        if os.path.isdir(os.path.join(full, name)):
            out.append(name)
    return out


def discover(repo_root):
    """Classify suites under repo_root (default: this script's repo).

    Raises InventoryError when discovery produces duplicate python labels
    or duplicate paths — the consumers treat this as fatal.
    """
    root = os.path.abspath(repo_root)
    unit_dirs = _immediate_subdirs(root, UNITS_DIR)

    twister = []
    exec_only = []
    python_unit = []
    for name in unit_dirs:
        unit_dir = os.path.join(root, UNITS_DIR, name)
        has_testcase = os.path.isfile(os.path.join(unit_dir, "testcase.yaml"))
        has_cmake = os.path.isfile(os.path.join(unit_dir, "CMakeLists.txt"))
        if has_testcase:
            twister.append(name)
        elif has_cmake:
            exec_only.append(name)
        else:
            # Python-only directory: every immediate test_*.py file is one
            # canonical child.  Non-test files (e.g. __init__.py) are not.
            for fname in sorted(os.listdir(unit_dir)):
                if fname.startswith(PYTEST_PREFIX) and fname.endswith(".py"):
                    python_unit.append(
                        PyChild(
                            label=name,
                            path=_posix(os.path.join(UNITS_DIR, name, fname)),
                        )
                    )

    python_scripts = []
    scripts_dir = os.path.join(root, SCRIPTS_DIR)
    if os.path.isdir(scripts_dir):
        for fname in sorted(os.listdir(scripts_dir)):
            if fname.startswith(PYTEST_PREFIX) and fname.endswith(".py"):
                rel = _posix(os.path.join(SCRIPTS_DIR, fname))
                if rel in SELF_EXCLUSIONS:
                    continue
                label = fname[len(PYTEST_PREFIX) : -3]
                python_scripts.append(PyChild(label=label, path=rel))

    children = tuple(python_unit + python_scripts)
    _validate(children)
    return Inventory(
        twister=tuple(twister),
        exec_only=tuple(exec_only),
        python_children=children,
        repo_root=root,
    )


def _validate(children):
    """Defensive duplicate detection for python children (labels and paths)."""
    labels = {}
    paths = {}
    for child in children:
        if child.label in labels:
            raise InventoryError(
                "duplicate python child label %r: %s and %s"
                % (child.label, labels[child.label], child.path)
            )
        labels[child.label] = child.path
        if child.path in paths:
            raise InventoryError("duplicate python child path %r" % child.path)
        paths[child.path] = True


def default_repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="deterministic suite inventory (twister/exec-only/python)"
    )
    parser.add_argument("--repo-root", default=default_repo_root())
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--twister", action="store_true", help="print twister suite names"
    )
    group.add_argument(
        "--exec-only", action="store_true", help="print exec-only suite names"
    )
    group.add_argument(
        "--python", action="store_true", help="print label<TAB>relpath python children"
    )
    group.add_argument(
        "--count", action="store_true", help="print total gate child count"
    )
    group.add_argument("--json", action="store_true", help="print full JSON record")
    args = parser.parse_args(argv)

    try:
        inv = discover(args.repo_root)
    except InventoryError as exc:
        print("test_inventory: FATAL: %s" % exc, file=sys.stderr)
        return 2
    except OSError as exc:
        print("test_inventory: FATAL: %s" % exc, file=sys.stderr)
        return 2

    if args.twister:
        for s in inv.twister:
            print(s)
    elif args.exec_only:
        for s in inv.exec_only:
            print(s)
    elif args.python:
        for child in inv.python_children:
            print("%s\t%s" % (child.label, child.path))
    elif args.count:
        print(inv.total())
    elif args.json:
        json.dump(inv.to_dict(), sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        # Default: human-readable summary (stable, sorted).
        print("twister: %d" % len(inv.twister))
        print("exec-only: %d" % len(inv.exec_only))
        print("python: %d" % len(inv.python_children))
        print("total: %d" % inv.total())
    return 0


if __name__ == "__main__":
    sys.exit(main())
