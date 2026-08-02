#!/usr/bin/env python3
"""T7 Stage 1: test-matrix manifest checker (stdlib only).

Validates tests/test-matrix.json against the production source inventory
and the current test suites.  See docs/pre-refactor-testing-t7-stage1-handoff.md
section 4 and docs/testing/coverage-matrix.md for the classification and
evidence semantics.

Rules enforced (all checked, deterministic sorted output, nonzero exit):
  1. exact source inventory completeness (no missing/duplicate/stale path)
  2. suite names resolve to existing unit directories, accepted bsim:stage1,
     production build commands, or named hardware scripts
  3. direct sources have at least one direct suite
  4. integration/delegated/hardware entries carry an explicit reason and one
     or more concrete acceptance commands/evidence paths
  5. historical source has only historical evidence and is marked excluded
     from numeric coverage
  6. header-structural entry names a structural suite
  7. every manifest witness string exists in referenced test source
  8. every listed public API exists in its production source/header
  9. no empty outcome/transition/exclusion placeholders
 10. with --coverage-json: every numeric-population direct source appears and
     every compiled function executes at least once, unless a precise function
     exclusion carries reason + evidence; duplicate variant records grouped by
     source/function — any executed variant satisfies, zero-hit alternate
     variants remain visible in the report (noted, not removed)

Usage:
  check-test-matrix.py [--manifest PATH] [--repo-root PATH] [--coverage-json PATH]
"""

import argparse
import glob
import json
import os
import re
import sys

ALLOWED_CLASSIFICATIONS = {
    "direct",
    "integration-only",
    "delegated-glue",
    "hardware-only",
    "historical-retired",
    "header-structural",
}

ALLOWED_EVIDENCE = {
    "direct",
    "integration",
    "build",
    "hardware",
    "structural",
    "historical",
}

BUILD_COMMANDS = {"fw-build-5340", "fw-build-54l15", "fw-build-dongle"}

REQUIRED_REASON_CLASSES = {"integration-only", "delegated-glue", "hardware-only"}

PRODUCTION_BUILD_COMMANDS = {"fw-build-5340", "fw-build-54l15", "fw-build-dongle"}

SOURCE_FILE_RE = re.compile(r"\.c$")


def _api_pattern(api):
    return re.compile(r"\b%s\s*\(" % re.escape(api))


ERROR = "error"
NOTE = "note"


class Checker:
    def __init__(self, repo_root, manifest_path, coverage_path=None):
        self.repo_root = os.path.abspath(repo_root)
        self.manifest_path = os.path.abspath(manifest_path)
        self.coverage_path = coverage_path
        self.messages = []  # (severity, text) sorted at the end

    # ---------- diagnostics ----------
    def error(self, text):
        self.messages.append((ERROR, text))

    def note(self, text):
        self.messages.append((NOTE, text))

    # ---------- inventory ----------
    def inventory(self):
        paths = []
        for root, _dirs, files in os.walk(os.path.join(self.repo_root, "src")):
            for f in files:
                full = os.path.join(root, f)
                rel = os.path.relpath(full, self.repo_root).replace(os.sep, "/")
                if SOURCE_FILE_RE.search(rel):
                    paths.append(rel)
        for extra in ("src/flpr_protocol.h", "dongle/hci_ipc/src/main.c"):
            full = os.path.join(self.repo_root, extra)
            if os.path.isfile(full):
                paths.append(extra)
        return sorted(paths)

    # ---------- suite resolution ----------
    def resolve_suite(self, name):
        """Return (kind, path-or-None) if resolvable, else None."""
        if not isinstance(name, str):
            return None
        unit = os.path.join(self.repo_root, "tests", "unit", name)
        if os.path.isdir(unit):
            return ("unit", unit)
        if name == "bsim:stage1":
            return ("bsim", os.path.join(self.repo_root, "tests", "bsim"))
        if name in PRODUCTION_BUILD_COMMANDS:
            return ("build-cmd", None)
        script = os.path.join(self.repo_root, "scripts", name)
        if os.path.isfile(script):
            return ("script", script)
        return None

    def suite_test_sources(self, name):
        """Resolve test source files for a suite (for witness checks)."""
        if not isinstance(name, str):
            return []
        resolved = self.resolve_suite(name)
        if resolved is None:
            return []
        kind, path = resolved
        if path is None:
            return []
        files = []
        if kind == "unit":
            cmake = os.path.join(path, "CMakeLists.txt")
            if os.path.isfile(cmake):
                files.extend(self._cmake_test_sources(cmake, path))
            files.extend(glob.glob(os.path.join(path, "src", "*.c")))
            files.extend(glob.glob(os.path.join(path, "*.c")))
        elif kind == "bsim":
            files.extend(glob.glob(os.path.join(path, "**", "*"), recursive=True))
            runner = os.path.join(self.repo_root, "scripts", "bsim-stage1-run.sh")
            if os.path.isfile(runner):
                files.append(runner)
        elif kind == "script":
            files.append(path)
        return sorted(
            os.path.abspath(f)
            for f in files
            if os.path.isfile(f) and not f.endswith(".gcno")
        )

    @staticmethod
    def _cmake_test_sources(cmake_path, suite_dir):
        """Parse target_sources(...) entries, expanding simple set() vars."""
        with open(cmake_path, "r", encoding="utf-8") as fh:
            text = fh.read()
        vars_ = dict(re.findall(r"set\(([A-Z0-9_]+)\s+([^\s)]+)\)", text))
        entries = []
        for m in re.finditer(r"target_sources\([^)]*\)", text, re.DOTALL):
            for entry in re.findall(
                r"^\s*([^\s#]+\.(?:c|h))$", m.group(0), re.MULTILINE
            ):
                for var, val in vars_.items():
                    entry = entry.replace("${%s}" % var, val)
                entry = entry.replace("${CMAKE_CURRENT_SOURCE_DIR}/", "")
                entry = entry.replace("${CMAKE_SOURCE_DIR}/", "")
                entry = entry.replace("${CMAKE_CURRENT_LIST_DIR}/", "")
                full = os.path.normpath(os.path.join(suite_dir, entry))
                if os.path.isfile(full):
                    entries.append(full)
        return entries

    # ---------- API existence ----------
    def api_exists(self, source_rel, api):
        candidates = []
        candidates.append(os.path.join(self.repo_root, source_rel))
        src_dir = os.path.dirname(os.path.join(self.repo_root, source_rel))
        if os.path.isdir(src_dir):
            candidates.extend(
                os.path.join(src_dir, f)
                for f in sorted(os.listdir(src_dir))
                if f.endswith((".c", ".h"))
            )
        for path in sorted(set(candidates)):
            if not os.path.isfile(path):
                continue
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    if _api_pattern(api).search(fh.read()):
                        return True
            except OSError:
                continue
        return False

    # ---------- numeric population ----------
    @staticmethod
    def in_numeric_population(entry):
        source = entry.get("source", "")
        return (
            source.startswith("src/")
            and source.endswith(".c")
            and not entry.get("excluded_from_numeric", False)
        )

    # ---------- checks ----------
    def check(self):
        self._check_inventory_and_schema()
        self._check_witnesses_and_apis()
        if self.coverage_path:
            self._check_coverage()
        self.messages.sort()
        errors = [t for sev, t in self.messages if sev == ERROR]
        notes = [t for sev, t in self.messages if sev == NOTE]
        for text in self.messages:
            print("%s: %s" % text)
        print("check-test-matrix: %d error(s), %d note(s)" % (len(errors), len(notes)))
        return 1 if errors else 0

    # ---------- rules 1, 2, 3, 4, 5, 6, 9 ----------
    def _check_inventory_and_schema(self):
        with open(self.manifest_path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        if isinstance(data, dict) and "entries" in data:
            entries = data["entries"]
        elif isinstance(data, list):
            entries = data
        else:
            self.error("manifest must be a list or {'entries': [...]}")
            return
        self.entries = entries

        inv = self.inventory()
        seen = {}
        for entry in entries:
            if not isinstance(entry, dict):
                self.error("malformed entry (not an object)")
                continue
            source = entry.get("source")
            if not isinstance(source, str) or not source:
                self.error("entry without 'source'")
                continue
            seen[source] = seen.get(source, 0) + 1

        for path in inv:
            if path not in seen:
                self.error("missing inventory entry: %s" % path)
        for source, count in sorted(seen.items()):
            if count > 1:
                self.error("duplicate manifest entry: %s" % source)
            if source not in inv:
                self.error("stale manifest entry: %s" % source)

        for entry in entries:
            source = entry.get("source")
            if not isinstance(source, str):
                continue
            classification = entry.get("classification")
            if classification not in ALLOWED_CLASSIFICATIONS:
                self.error("bad classification: %s: %r" % (source, classification))
            suites = entry.get("suites")
            if not isinstance(suites, list) or not suites:
                self.error("missing suites: %s" % source)
                continue
            for suite in suites:
                name = suite.get("name") if isinstance(suite, dict) else None
                evidence = suite.get("evidence") if isinstance(suite, dict) else None
                if not isinstance(name, str) or self.resolve_suite(name) is None:
                    self.error("unresolvable suite: %s: %s" % (source, name))
                if evidence not in ALLOWED_EVIDENCE:
                    self.error("bad evidence: %s: %s: %r" % (source, name, evidence))

            if classification == "direct":
                if not any(
                    s.get("evidence") == "direct" for s in suites if isinstance(s, dict)
                ):
                    self.error("direct source without direct suite: %s" % source)
            elif classification == "header-structural":
                if not any(
                    s.get("evidence") == "structural"
                    for s in suites
                    if isinstance(s, dict)
                ):
                    self.error(
                        "header-structural without structural suite: %s" % source
                    )
            elif classification == "historical-retired":
                for s in suites:
                    if isinstance(s, dict) and s.get("evidence") != "historical":
                        self.error(
                            "historical suite evidence not historical: %s: %s"
                            % (source, s.get("name"))
                        )
                if not entry.get("excluded_from_numeric", False):
                    self.error(
                        "historical source not excluded from numeric: %s" % source
                    )
            elif classification in REQUIRED_REASON_CLASSES:
                reason = entry.get("reason")
                if not isinstance(reason, str) or not reason.strip():
                    self.error(
                        "missing reason: %s (classification %s)"
                        % (source, classification)
                    )
                acceptance = entry.get("hardware_acceptance")
                if not isinstance(acceptance, list) or not acceptance:
                    self.error(
                        "missing acceptance: %s (classification %s)"
                        % (source, classification)
                    )
                else:
                    for item in acceptance:
                        if not isinstance(item, str) or not item:
                            self.error("empty acceptance item: %s" % source)
                            continue
                        if self.resolve_suite(item) is not None:
                            continue
                        full = os.path.join(self.repo_root, item)
                        if os.path.exists(full):
                            continue
                        self.error("unresolvable acceptance: %s: %s" % (source, item))

            for idx, outcome in enumerate(entry.get("public_outcomes", [])):
                if not isinstance(outcome, dict):
                    self.error("malformed outcome: %s: index %d" % (source, idx))
                    continue
                for field in ("api", "outcome", "witness"):
                    if not isinstance(outcome.get(field), str) or not outcome[field]:
                        self.error(
                            "empty outcome placeholder: %s: outcome %d (%s)"
                            % (source, idx, field)
                        )
            for idx, trans in enumerate(entry.get("state_transitions", [])):
                if not isinstance(trans, dict):
                    self.error("malformed transition: %s: index %d" % (source, idx))
                    continue
                for field in ("transition", "witness"):
                    if not isinstance(trans.get(field), str) or not trans[field]:
                        self.error(
                            "empty transition placeholder: %s: transition %d (%s)"
                            % (source, idx, field)
                        )
            for idx, excl in enumerate(entry.get("function_exclusions", [])):
                if not isinstance(excl, dict):
                    self.error("malformed exclusion: %s: index %d" % (source, idx))
                    continue
                for field in ("function", "reason", "evidence"):
                    if not isinstance(excl.get(field), str) or not excl[field]:
                        self.error(
                            "empty exclusion placeholder: %s: exclusion %d (%s)"
                            % (source, idx, field)
                        )

    # ---------- rules 7, 8 ----------
    def _check_witnesses_and_apis(self):
        for entry in self.entries:
            source = entry.get("source")
            if not isinstance(source, str):
                continue
            suite_sources = []
            for suite in entry.get("suites", []):
                if isinstance(suite, dict):
                    suite_sources.extend(self.suite_test_sources(suite.get("name")))
            suite_sources = sorted(set(suite_sources))
            contents = {}
            for path in suite_sources:
                try:
                    with open(path, "r", encoding="utf-8", errors="replace") as fh:
                        contents[path] = fh.read()
                except OSError:
                    continue
            all_text = "\n".join(contents.values())

            witnesses = []
            for outcome in entry.get("public_outcomes", []):
                if isinstance(outcome, dict) and outcome.get("witness"):
                    witnesses.append(outcome["witness"])
            for trans in entry.get("state_transitions", []):
                if isinstance(trans, dict) and trans.get("witness"):
                    witnesses.append(trans["witness"])

            for witness in witnesses:
                if not suite_sources:
                    self.error(
                        "witness without test source: %s: %r" % (source, witness)
                    )
                elif witness not in all_text:
                    self.error("invented witness: %s: %r" % (source, witness))

            for outcome in entry.get("public_outcomes", []):
                if isinstance(outcome, dict) and outcome.get("api"):
                    if not self.api_exists(source, outcome["api"]):
                        self.error("invented API: %s: %r" % (source, outcome["api"]))

    # ---------- rule 10 ----------
    def _check_coverage(self):
        coverage_path = self.coverage_path
        if not coverage_path:
            return
        with open(coverage_path, "r", encoding="utf-8") as fh:
            cov = json.load(fh)
        files = cov.get("files", []) if isinstance(cov, dict) else []
        by_path = {}
        for rec in files:
            by_path[rec.get("file")] = rec

        for entry in self.entries:
            source = entry.get("source")
            if not self.in_numeric_population(entry):
                continue
            rec = by_path.get(source)
            if rec is None:
                self.error("absent from coverage: %s" % source)
                continue
            excluded = {
                e.get("function")
                for e in entry.get("function_exclusions", [])
                if isinstance(e, dict)
            }
            groups = {}
            for fn in rec.get("functions", []):
                name = fn.get("name") or fn.get("demangled_name")
                groups.setdefault(name, []).append(fn)
            for name in sorted(groups):
                counts = [fn.get("execution_count", 0) for fn in groups[name]]
                executed = max(counts) > 0
                zero_variants = sum(1 for c in counts if c == 0)
                if executed and zero_variants:
                    self.note(
                        "zero-hit variant remains visible: %s: %s (hit in %d/%d variant(s))"
                        % (source, name, len(counts) - zero_variants, len(counts))
                    )
                elif not executed:
                    if name in excluded:
                        continue
                    self.error("zero-hit function: %s: %s" % (source, name))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default="tests/test-matrix.json")
    parser.add_argument("--repo-root", default=os.getcwd())
    parser.add_argument("--coverage-json", default=None)
    args = parser.parse_args(argv)

    repo_root = os.path.abspath(args.repo_root)
    manifest_path = (
        os.path.join(repo_root, args.manifest)
        if not os.path.isabs(args.manifest)
        else args.manifest
    )
    if not os.path.isfile(manifest_path):
        print("check-test-matrix: FATAL: manifest not found: %s" % manifest_path)
        return 2
    coverage_path = None
    if args.coverage_json:
        coverage_path = args.coverage_json
        if not os.path.isfile(coverage_path):
            print(
                "check-test-matrix: FATAL: coverage json not found: %s" % coverage_path
            )
            return 2

    checker = Checker(repo_root, manifest_path, coverage_path)
    return checker.check()


if __name__ == "__main__":
    sys.exit(main())
