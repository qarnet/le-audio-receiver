#!/usr/bin/env python3
"""Public-boundary tests for FR2: project-version CLI and firmware-build
workflow (stdlib unittest).

Tests 1-4 exercise the real ``scripts/project-version.py`` CLI as a
subprocess against the repository ``VERSION`` and temporary fixtures.
Tests 5-8 statically validate the committed workflow YAML text: the
workflow is declarative public behavior and hosted execution is unavailable
before remote push.  No third-party YAML parser, no private-helper
assertions, and no mock of the workflow.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION_SCRIPT = os.path.join(REPO_ROOT, "scripts", "project-version.py")
WORKFLOW = os.path.join(REPO_ROOT, ".github", "workflows", "firmware-build.yml")

ERROR_PREFIX = "project-version: error: "

GOOD_VERSION = (
    "VERSION_MAJOR = 0\n"
    "VERSION_MINOR = 1\n"
    "PATCHLEVEL = 0\n"
    "VERSION_TWEAK = 0\n"
    "EXTRAVERSION =\n"
)

CONTAINER_IMAGE = (
    "ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:"
    "f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276"
)
CHECKOUT_SHA = "3d3c42e5aac5ba805825da76410c181273ba90b1"
UPLOAD_SHA = "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"
NRF_COMMIT = "ba167d9f3db4abbdc9b67887ca3ea66c64f2d956"


def run_version_script(extra_args=(), cwd=REPO_ROOT):
    cmd = [sys.executable, VERSION_SCRIPT] + list(extra_args)
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


def write_version(tmp, content):
    path = os.path.join(tmp, "VERSION")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(content)
    return path


def workflow_text():
    with open(WORKFLOW, "r", encoding="utf-8") as fh:
        return fh.read()


def workflow_lines():
    return workflow_text().splitlines()


class TestProjectVersionCli(unittest.TestCase):
    """Tests 1-4: version reader observable behavior."""

    def test_repo_version_is_0_1_0(self):
        res = run_version_script()
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(res.stdout, "0.1.0\n")
        self.assertEqual(res.stderr, "")

    def test_valid_explicit_fixture_prints_canonical_semantic_version(self):
        cases = {
            "1.2.3": (
                "VERSION_MAJOR = 1\n"
                "VERSION_MINOR = 2\n"
                "PATCHLEVEL = 3\n"
                "VERSION_TWEAK = 0\n"
                "EXTRAVERSION =\n"
            ),
            "0.0.0": (
                "VERSION_MAJOR = 0\n"
                "VERSION_MINOR = 0\n"
                "PATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\n"
                "EXTRAVERSION =\n"
            ),
            "10.20.255": (
                "VERSION_MAJOR = 10\n"
                "VERSION_MINOR = 20\n"
                "PATCHLEVEL = 255\n"
                "VERSION_TWEAK = 0\n"
                "EXTRAVERSION =\n"
            ),
        }
        for expected, content in cases.items():
            with self.subTest(expected=expected):
                with tempfile.TemporaryDirectory() as tmp:
                    path = write_version(tmp, content)
                    res = run_version_script(["--version-file", path], cwd=tmp)
                    self.assertEqual(res.returncode, 0, res.stderr)
                    self.assertEqual(res.stdout, expected + "\n")
                    self.assertEqual(res.stderr, "")

    def test_blank_lines_and_comments_allowed(self):
        content = (
            "# repository version\n"
            "\n"
            "VERSION_MAJOR = 0\n"
            "\n"
            "# comment\n"
            "VERSION_MINOR = 1\n"
            "PATCHLEVEL = 0\n"
            "VERSION_TWEAK = 0\n"
            "EXTRAVERSION =\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            path = write_version(tmp, content)
            res = run_version_script(["--version-file", path], cwd=tmp)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stdout, "0.1.0\n")

    def test_invalid_version_files_fail_cleanly(self):
        with tempfile.TemporaryDirectory() as tmp:
            missing = os.path.join(tmp, "does-not-exist")
            cases = [("missing", ["--version-file", missing], None)]
            empty = write_version(tmp, "")
            cases.append(("empty", ["--version-file", empty], None))
            non_utf8 = os.path.join(tmp, "non-utf8")
            with open(non_utf8, "wb") as fh:
                fh.write(b"\xff\xfe\x00 not text\n")
            cases.append(("non_utf8", ["--version-file", non_utf8], None))
            real = write_version(tmp, GOOD_VERSION)
            symlink = os.path.join(tmp, "link")
            os.symlink(real, symlink)
            cases.append(("symlink", ["--version-file", symlink], None))
            unreadable = write_version(tmp, GOOD_VERSION)
            os.chmod(unreadable, 0)
            cases.append(("unreadable", ["--version-file", unreadable], None))
            for name, args, _ in cases:
                with self.subTest(variant=name):
                    res = run_version_script(args, cwd=tmp)
                    self.assertNotEqual(res.returncode, 0)
                    self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
                    self.assertNotIn("Traceback", res.stderr)
                    self.assertEqual(res.stdout, "")

    def test_invalid_version_content_fails_cleanly(self):
        cases = {
            "missing_key": (
                "VERSION_MAJOR = 0\nVERSION_MINOR = 1\nVERSION_TWEAK = 0\n"
                "EXTRAVERSION =\n"
            ),
            "duplicate_key": (
                "VERSION_MAJOR = 0\nVERSION_MAJOR = 1\nVERSION_MINOR = 1\n"
                "PATCHLEVEL = 0\nVERSION_TWEAK = 0\nEXTRAVERSION =\n"
            ),
            "unknown_key": (
                "VERSION_MAJOR = 0\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION =\nFOO = 1\n"
            ),
            "malformed_assignment": (
                "VERSION_MAJOR 0\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION =\n"
            ),
            "no_equals": (
                "VERSION_MAJOR\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION =\n"
            ),
            "leading_zero": (
                "VERSION_MAJOR = 01\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION =\n"
            ),
            "over_255": (
                "VERSION_MAJOR = 0\nVERSION_MINOR = 1\nPATCHLEVEL = 256\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION =\n"
            ),
            "negative": (
                "VERSION_MAJOR = -1\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION =\n"
            ),
            "nonzero_tweak": (
                "VERSION_MAJOR = 0\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 1\nEXTRAVERSION =\n"
            ),
            "nonempty_extraversion": (
                "VERSION_MAJOR = 0\nVERSION_MINOR = 1\nPATCHLEVEL = 0\n"
                "VERSION_TWEAK = 0\nEXTRAVERSION = -rc1\n"
            ),
        }
        for name, content in cases.items():
            with self.subTest(variant=name):
                with tempfile.TemporaryDirectory() as tmp:
                    path = write_version(tmp, content)
                    res = run_version_script(["--version-file", path], cwd=tmp)
                    self.assertNotEqual(res.returncode, 0)
                    self.assertTrue(res.stderr.startswith(ERROR_PREFIX), res.stderr)
                    self.assertNotIn("Traceback", res.stderr)


class TestWorkflowContract(unittest.TestCase):
    """Tests 5-6: event/permission and immutable-pin contract."""

    def test_event_and_permission_contract(self):
        text = workflow_text()
        self.assertIn("pull_request:", text, "normal PR trigger missing")
        self.assertIn("workflow_dispatch:", text, "manual dispatch missing")
        push_match = re.search(r"(?ms)^\s*push:\s*\n(.*?)^\S", text)
        self.assertIsNotNone(push_match, "push trigger missing")
        push_block = push_match.group(1) if push_match else ""
        self.assertIn("main", push_block, "main branch push missing")
        other_branches = [
            line.split("-", 1)[1].strip()
            for line in push_block.splitlines()
            if line.strip().startswith("-")
        ]
        self.assertEqual(other_branches, ["main"], "push must target main only")
        self.assertIn("permissions:", text)
        self.assertIn("contents: read", text)
        for forbidden in (
            "pull_request_target",
            "workflow_run",
            "contents: write",
            "id-token",
            "secrets:",
            "environment:",
            "privileged",
            "ACCEPT_JLINK_LICENSE",
        ):
            self.assertNotIn(forbidden, text, "forbidden %r present" % forbidden)

    def test_pinned_runner_container_and_actions(self):
        text = workflow_text()
        self.assertIn("runs-on: ubuntu-22.04", text)
        self.assertIn(CONTAINER_IMAGE, text)
        self.assertIn("defaults:", text)
        self.assertIn("shell: bash", text)
        self.assertIn("actions/checkout@%s" % CHECKOUT_SHA, text)
        self.assertIn("actions/upload-artifact@%s" % UPLOAD_SHA, text)
        self.assertIn("ref: %s" % NRF_COMMIT, text)
        self.assertIn("nrf/VERSION", text)
        self.assertIn('"3.3.0"', text)
        for line in workflow_lines():
            stripped = line.strip()
            if stripped.startswith("uses:"):
                self.assertRegex(
                    stripped,
                    r"^uses: [a-zA-Z0-9._-]+/[a-zA-Z0-9._-]+@[0-9a-f]{40}$",
                    "mutable or unpinned uses: reference: %s" % stripped,
                )
        uses_lines = [
            line.strip()
            for line in workflow_lines()
            if line.strip().startswith("uses:")
        ]
        self.assertEqual(len(uses_lines), 3, "expected two checkouts plus one upload")


class TestWorkflowCommands(unittest.TestCase):
    """Test 7: exact workspace, build, contract, version, and package steps."""

    def test_workspace_setup_commands_exact(self):
        text = workflow_text()
        for command in (
            "west init -l nrf",
            "west update --narrow -o=--depth=1",
            "west zephyr-export",
            "west topdir",
            "git -C nrf rev-parse HEAD",
            NRF_COMMIT,
        ):
            self.assertIn(command, text, "missing %r" % command)

    def test_build_contract_and_version_steps_exact(self):
        text = workflow_text()
        for command in (
            "./scripts/bin/fw-build-5340",
            "./scripts/bin/fw-build-54l15",
            "scripts/check-build-contract.py",
            "--nrf5340 build/nrf5340",
            "--nrf54l15 build/nrf54l15",
            "build/nrf5340/le-audio-receiver/zephyr/include/generated/zephyr/app_version.h",
            "build/nrf54l15/le-audio-receiver/zephyr/include/generated/zephyr/app_version.h",
            "APP_VERSION_STRING",
            "0\\.1\\.0",
        ):
            self.assertIn(command, text, "missing %r" % command)

    def test_project_identity_and_packager_inputs_exact(self):
        text = workflow_text()
        for needle in (
            "scripts/project-version.py",
            '"$GITHUB_SHA"',
            '"$GITHUB_OUTPUT"',
            'test "$version" = "0.1.0"',
            "version=$version",
            "scripts/package-firmware-release.py",
            "--git-commit",
            "--ncs-version v3.3.0",
            "--build-root build",
            "--output-dir dist",
            '"$version"',
        ):
            self.assertIn(needle, text, "missing %r" % needle)


class TestWorkflowArtifactContract(unittest.TestCase):
    """Test 8: checksum/ZIP verification and upload contract."""

    def test_checksum_and_zip_verification(self):
        text = workflow_text()
        self.assertIn("sha256sum --strict -c SHA256SUMS", text)
        for zip_name in (
            "le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip",
            "le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip",
        ):
            self.assertIn("python3 -m zipfile --test dist/%s" % zip_name, text)

    def test_upload_contract_exact_files_and_options(self):
        text = workflow_text()
        self.assertIn(
            "name: firmware-v${{ steps.project-version.outputs.version }}-${{ github.sha }}",
            text,
        )
        self.assertIn("if-no-files-found: error", text)
        self.assertIn("retention-days: 14", text)
        self.assertIn("compression-level: 0", text)
        self.assertIn("overwrite: false", text)
        self.assertNotIn("archive:", text, "archive option must not be used")
        upload_lines = workflow_lines()
        path_index = next(i for i, line in enumerate(upload_lines) if "path: |" in line)
        listed = []
        for line in upload_lines[path_index + 1 :]:
            if not line.strip().startswith("dist/"):
                break
            listed.append(line.strip())
        self.assertEqual(
            listed,
            [
                "dist/le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip",
                "dist/le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip",
                "dist/SHA256SUMS",
            ],
            "upload path must list exactly the three files individually",
        )
        self.assertNotIn("dist/*", text, "wildcard upload path forbidden")


if __name__ == "__main__":
    unittest.main()
