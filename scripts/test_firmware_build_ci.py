#!/usr/bin/env python3
"""Public-boundary tests for FR2+FR3: project-version CLI and firmware-build
workflow (stdlib unittest).

Tests 1-4 exercise the real ``scripts/project-version.py`` CLI as a
subprocess against the repository ``VERSION`` and temporary fixtures.
Tests 5-8 statically validate the committed workflow YAML text: the
workflow is declarative public behavior and hosted execution is unavailable
before remote push.  Tests 9+ extend the same static validation to the FR3
trusted-main release job: automatic version-tag initiation from a VERSION
change on main, fail-closed identity and collision probes, creation of an
untagged draft at the exact main commit (GitHub creates the git tag only at
manual publication), and post-create draft verification.  No third-party
YAML parser, no private-helper assertions, and no mock of the workflow.
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
DOWNLOAD_SHA = "3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c"
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


def run_scripts():
    """Extract every ``run: |`` block body as one string, so run-script
    content can be checked without expression interpolation."""
    lines = workflow_lines()
    scripts = []
    for i, line in enumerate(lines):
        if not re.match(r"^\s*run: \|", line):
            continue
        indent = len(line) - len(line.lstrip())
        body = []
        for following in lines[i + 1 :]:
            if not following.strip():
                body.append("")
                continue
            if len(following) - len(following.lstrip()) <= indent:
                break
            body.append(following)
        scripts.append("\n".join(body))
    return scripts


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

    @staticmethod
    def _dash_values(block, key):
        """Values of one ``key:`` list inside a YAML block."""
        marker = re.search(r"^\s*%s:\s*$" % key, block, re.MULTILINE)
        if not marker:
            return []
        values = []
        for line in block[marker.end() :].splitlines():
            stripped = line.strip()
            if stripped.startswith("-"):
                values.append(stripped[1:].strip())
            elif stripped:
                break
        return values

    def test_event_and_permission_contract(self):
        text = workflow_text()
        self.assertIn("pull_request:", text, "normal PR trigger missing")
        self.assertIn("workflow_dispatch:", text, "manual dispatch missing")
        push_match = re.search(r"(?ms)^\s*push:\s*\n(.*?)^\S", text)
        self.assertIsNotNone(push_match, "push trigger missing")
        push_block = push_match.group(1) if push_match else ""
        self.assertEqual(
            self._dash_values(push_block, "branches"),
            ["main"],
            "push branches must be main only",
        )
        self.assertEqual(
            self._dash_values(push_block, "tags"),
            [],
            "push must have no tags block or pattern",
        )
        self.assertNotIn("'v*'", text, "quoted tag pattern must be removed entirely")
        self.assertIn("permissions:", text)
        self.assertIn("contents: read", text)
        self.assertEqual(
            text.count("contents: write"),
            1,
            "contents: write must exist only on the release job",
        )
        for forbidden in (
            "pull_request_target",
            "workflow_run",
            "id-token",
            "secrets:",
            "environment:",
            "privileged",
            "ACCEPT_JLINK_LICENSE",
        ):
            self.assertNotIn(forbidden, text, "forbidden %r present" % forbidden)
        for script in run_scripts():
            self.assertNotIn(
                "${{", script, "direct expression interpolation in run script"
            )

    def test_concurrency_cancellation_pr_only(self):
        text = workflow_text()
        self.assertIn(
            "cancel-in-progress: ${{ github.event_name == 'pull_request' }}",
            text,
            "only PR runs may cancel; trusted main runs must not be cancelled",
        )
        self.assertNotIn(
            "cancel-in-progress: true",
            text,
            "main runs must never be cancelled by a newer run",
        )

    def test_pinned_runner_container_and_actions(self):
        text = workflow_text()
        self.assertIn("runs-on: ubuntu-22.04", text)
        self.assertIn(CONTAINER_IMAGE, text)
        self.assertIn("defaults:", text)
        self.assertIn("shell: bash", text)
        self.assertIn("actions/checkout@%s" % CHECKOUT_SHA, text)
        self.assertIn("actions/upload-artifact@%s" % UPLOAD_SHA, text)
        self.assertIn("actions/download-artifact@%s" % DOWNLOAD_SHA, text)
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
        self.assertEqual(
            len(uses_lines),
            5,
            "expected two checkouts, one upload, one download, and one "
            "release checkout",
        )


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

    def test_zephyr_base_export_after_workspace_verification(self):
        text = workflow_text()
        for line in (
            'zephyr_base="$GITHUB_WORKSPACE/workspace/zephyr"',
            'test -d "$zephyr_base"',
            'printf \'ZEPHYR_BASE=%s\\n\' "$zephyr_base" >> "$GITHUB_ENV"',
        ):
            self.assertIn(line, text, "missing %r" % line)
        env_index = text.index('"$GITHUB_ENV"')
        for command in (
            "west init -l nrf",
            "west update --narrow -o=--depth=1",
            "west zephyr-export",
            "west topdir",
        ):
            self.assertLess(
                text.index(command),
                env_index,
                "ZEPHYR_BASE export must appear after %r" % command,
            )
        self.assertNotIn(
            "ZEPHYR_BASE=",
            text[: text.index("west init -l nrf")],
            "ZEPHYR_BASE must not be set before west init",
        )

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
        self.assertIn(
            'test "$(find dist -mindepth 1 -maxdepth 1 | wc -l)" -eq 3',
            text,
            "exact total top-level entry count check missing",
        )
        self.assertIn(
            'test "$(find dist -mindepth 1 -maxdepth 1 -type f | wc -l)" -eq 3',
            text,
            "exact regular-file top-level count check missing",
        )
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
            if not line.strip().startswith("workspace/le-audio-receiver/dist/"):
                break
            listed.append(line.strip())
        self.assertEqual(
            listed,
            [
                "workspace/le-audio-receiver/dist/le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip",
                "workspace/le-audio-receiver/dist/le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip",
                "workspace/le-audio-receiver/dist/SHA256SUMS",
            ],
            "upload path must list exactly the three workspace-root-relative "
            "files individually",
        )
        for line in upload_lines[path_index + 1 :]:
            if not line.strip():
                break
            self.assertFalse(
                line.strip().startswith("dist/"),
                "bare workspace-root dist/ upload entry: %s" % line.strip(),
            )
        self.assertNotIn("dist/*", text, "wildcard upload path forbidden")


class TestReleaseJobContract(unittest.TestCase):
    """FR3 tests 9+: trusted-main automatic draft-release job static
    contract."""

    def _release_block(self):
        text = workflow_text()
        marker = re.search(r"(?ms)^  release:\s*\n", text)
        if marker is None:
            raise AssertionError("release job missing")
        start = marker.end()
        tail = text[start:]
        m = re.search(r"(?m)^\S", tail)
        end = len(tail)
        if m is not None:
            end = m.start()
        return text[start : start + end]

    def test_release_job_guard_and_topology(self):
        block = self._release_block()
        self.assertIn("needs: firmware", block, "release must need firmware")
        self.assertIn(
            "if: github.event_name == 'push' && github.ref == 'refs/heads/main' "
            "&& needs.firmware.outputs.release-requested == 'true'",
            block,
            "release must run only on a trusted main push that changed VERSION",
        )
        self.assertIn("runs-on: ubuntu-22.04", block)
        self.assertIn("timeout-minutes: 15", block)
        self.assertNotIn("container:", block, "release job must have no container")
        self.assertIn("contents: write", block)
        self.assertNotIn(
            "contents: read",
            block.split("contents: write")[0] or "x",
            "release job must not set a broader read permission",
        )
        self.assertNotIn("pull_request_target", block)
        self.assertNotIn("environment:", block)
        self.assertNotIn("privileged", block)

    def test_firmware_job_version_output_and_release_decision(self):
        text = workflow_text()
        self.assertIn(
            "version: ${{ steps.project-version.outputs.version }}",
            text,
            "firmware job must expose the validated version output",
        )
        self.assertIn(
            "release-requested: ${{ steps.project-version.outputs.release-requested }}",
            text,
            "firmware job must expose the release-requested output",
        )
        for needle in (
            "BEFORE_SHA: ${{ github.event.before }}",
            "release_requested=false",
            'if [[ "$GITHUB_EVENT_NAME" == push && "$GITHUB_REF" == refs/heads/main ]]; then',
            '[[ "$BEFORE_SHA" =~ ^[0-9a-f]{40}$ ]]',
            'test "$BEFORE_SHA" != 0000000000000000000000000000000000000000',
            'git rev-parse --verify --quiet "$BEFORE_SHA^{commit}" >/dev/null',
            'git diff --quiet "$BEFORE_SHA" "$GITHUB_SHA" -- VERSION',
            "diff_status=$?",
            'if [ "$diff_status" -eq 1 ]; then',
            "release_requested=true",
            'elif [ "$diff_status" -ne 0 ]; then',
            'echo "release-requested=$release_requested" >> "$GITHUB_OUTPUT"',
            'test "$version" = "0.1.0"',
        ):
            self.assertIn(needle, text, "missing %r" % needle)
        self.assertNotIn("refs/tags/*", text, "old tag-push guard must be removed")

    def test_release_identity_step_exact(self):
        text = workflow_text()
        for needle in (
            "Verify release identity",
            "FW_VERSION: ${{ needs.firmware.outputs.version }}",
            "RELEASE_REQUESTED: ${{ needs.firmware.outputs.release-requested }}",
            'version="$(python3 scripts/project-version.py)"',
            'tag="v$version"',
            'test "$version" = "$FW_VERSION"',
            'test "$RELEASE_REQUESTED" = "true"',
            'test "$GITHUB_EVENT_NAME" = "push"',
            'test "$GITHUB_REF" = "refs/heads/main"',
            'test "$GITHUB_REF_NAME" = "main"',
            'test "$(git rev-parse HEAD)" = "$GITHUB_SHA"',
            'echo "version=$version" >> "$GITHUB_OUTPUT"',
            'echo "tag=$tag" >> "$GITHUB_OUTPUT"',
        ):
            self.assertIn(needle, text, "missing %r" % needle)
        # No pre-existing tag check belongs here: collision is proven by the
        # fail-closed API probes, and no local tag exists before creation.
        self.assertNotIn(
            "git rev-list", text, "identity step must not check a local tag"
        )

    def test_download_contract_exact(self):
        text = workflow_text()
        self.assertIn("actions/download-artifact@%s" % DOWNLOAD_SHA, text)
        self.assertIn(
            "name: firmware-v${{ needs.firmware.outputs.version }}-${{ github.sha }}",
            text,
            "release download must use the exact firmware artifact name",
        )
        self.assertIn("path: dist", text)
        self.assertIn("digest-mismatch: error", text)
        self.assertNotIn("run-id:", text, "release download must use current run")
        self.assertNotIn("skip-decompress:", text)

    def test_prepare_inputs_exact(self):
        text = workflow_text()
        for needle in (
            "scripts/prepare-draft-release.py",
            "--tag",
            "--version",
            '--git-commit "$GITHUB_SHA"',
            "--ncs-version v3.3.0",
            '--repository "$REPOSITORY"',
            '--workflow "$WORKFLOW"',
            '--workflow-ref "$WORKFLOW_REF"',
            '--run-id "$RUN_ID"',
            '--run-attempt "$RUN_ATTEMPT"',
            "--artifact-dir dist",
            "--output-dir release-metadata",
            "RUN_ID: ${{ github.run_id }}",
            "RUN_ATTEMPT: ${{ github.run_attempt }}",
            "REPOSITORY: ${{ github.repository }}",
            "WORKFLOW: ${{ github.workflow }}",
            "WORKFLOW_REF: ${{ github.workflow_ref }}",
        ):
            self.assertIn(needle, text, "missing %r" % needle)

    def test_create_command_exact(self):
        text = workflow_text()
        for needle in (
            'gh release create "$tag"',
            '--target "$GITHUB_SHA"',
            "--draft",
            '--title "LE Audio Receiver $tag"',
            "--notes-file release-metadata/release-notes.md",
            '"dist/le-audio-receiver-v${version}-nrf5340-e83-factory.zip"',
            '"dist/le-audio-receiver-v${version}-nrf54l15-xiao-factory.zip"',
            "dist/SHA256SUMS",
            "release-metadata/release-provenance.json",
        ):
            self.assertIn(needle, text, "missing %r" % needle)
        for forbidden in (
            "--verify-tag",
            "--generate-notes",
            "--latest",
            "--prerelease",
            "--clobber",
            "gh release edit",
            "gh release delete",
            "gh release upload",
        ):
            self.assertNotIn(forbidden, text, "forbidden %r present" % forbidden)

    def test_existing_release_probe_fail_closed(self):
        text = workflow_text()
        self.assertIn("gh api --include", text)
        self.assertIn("repos/$GITHUB_REPOSITORY/releases/tags/$tag", text)
        self.assertIn("repos/$GITHUB_REPOSITORY/git/ref/tags/$tag", text)
        self.assertIn("release_status", text)
        self.assertIn("tag_status", text)
        self.assertIn("::error::release already exists for tag", text)
        self.assertIn("::error::existing-release probe failed for tag", text)
        self.assertIn("::error::git tag already exists for tag", text)
        self.assertIn("::error::existing-tag probe failed for tag", text)
        self.assertIn("404", text, "probes must permit only an exact HTTP 404")
        self.assertRegex(
            text,
            r"\^HTTP/\[0-9\.\]\+ 404",
            "probes must match an exact HTTP 404 status line",
        )
        self.assertIn("trap", text, "private probe responses must be trapped")
        self.assertIn('rm -f "$release_probe" "$tag_probe"', text)
        self.assertIn("exit 1", text)

    def test_post_create_verification_exact(self):
        text = workflow_text()
        for needle in (
            'gh release view "$tag" --json tagName,isDraft,isPrerelease,assets,targetCommitish,url',
            'assert release["tagName"] == tag',
            'assert release["isDraft"] is True',
            'assert release["isPrerelease"] is False',
            'assert release["targetCommitish"] == sha',
            "assert names == expected, names",
            'print("draft release URL: %s" % release["url"])',
            '"SHA256SUMS",',
            '"release-provenance.json",',
            "le-audio-receiver-v%s-nrf5340-e83-factory.zip",
            "le-audio-receiver-v%s-nrf54l15-xiao-factory.zip",
        ):
            self.assertIn(needle, text, "missing %r" % needle)
        # Draft releases are untagged (GitHub creates the git tag only at
        # publication), so the post-create check must never query the git
        # ref or read a tag-check file.
        for forbidden in (
            "--json tag,isDraft",
            'data["tag"] ==',
            'data["html_url"]',
            "html_url",
            "--verify-tag",
            "tag-check.json",
            "tag_data",
        ):
            self.assertNotIn(forbidden, text, "forbidden %r present" % forbidden)

    def test_git_ref_endpoint_only_in_preflight_probe(self):
        text = workflow_text()
        self.assertEqual(
            text.count("git/ref/tags/$tag"),
            1,
            "git-ref endpoint must appear exactly once, in the pre-write "
            "collision probe only",
        )
        self.assertIn(
            'gh api --include "repos/$GITHUB_REPOSITORY/git/ref/tags/$tag" >"$tag_probe" 2>&1',
            text,
            "pre-write tag absence probe must remain",
        )
        self.assertNotIn(
            "> tag-check.json",
            text,
            "post-create must not query the git ref (drafts are untagged)",
        )

    def test_no_forbidden_release_configuration(self):
        text = workflow_text()
        for forbidden in (
            "--generate-notes",
            "--latest",
            "--prerelease",
            "--clobber",
            "gh release edit",
            "pull_request_target",
            "workflow_run",
            "id-token",
            "privileged",
            "ACCEPT_JLINK_LICENSE",
            "permissions: write-all",
            "actions: write",
            "environment:",
        ):
            self.assertNotIn(forbidden, text, "forbidden %r present" % forbidden)


if __name__ == "__main__":
    unittest.main()
