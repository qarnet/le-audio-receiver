#!/usr/bin/env python3
"""Pure/fake unit tests for the PacedWriter teardown contract.

Proves (requirement 6 of the T8 review-fix):
  - writes continue during a delayed MediaTransport Release;
  - writes stop once the transport fds become invalid;
  - no writes after stop;
  - errors are captured and bounded;
  - join is always bounded.
"""

import os
import sys
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))
import bap_central_writer  # noqa: E402


class FakeFd:
    """A real pipe write end (int fd).  os.write is patched module-wide
    in setUp to count writes; close() makes it raise OSError (like a
    released MediaTransport fd)."""

    def __init__(self):
        self._r, self._w = os.pipe()
        self.writes = 0
        self.bytes = 0
        self.closed = False

    def close(self):
        if not self.closed:
            self.closed = True
            os.close(self._w)
            os.close(self._r)

    def drain(self):
        """Discard pending pipe bytes without blocking."""
        try:
            os.set_blocking(self._r, False)
            while os.read(self._r, 65536):
                pass
        except OSError:
            pass


class PacedWriterTest(unittest.TestCase):
    def setUp(self):
        self.fds = [FakeFd(), FakeFd()]
        self.frame_no = [0, 0]
        self._real_write = bap_central_writer.os.write

        def counting_write(fd, data):
            if isinstance(fd, FakeFd):
                if fd.closed:
                    raise OSError("closed")
                fd.writes += 1
                fd.bytes += len(data)
                return len(data)
            return self._real_write(fd, data)

        bap_central_writer.os.write = counting_write

    def tearDown(self):
        bap_central_writer.os.write = self._real_write
        for fd in self.fds:
            fd.close()

    def encode(self):
        return [
            (self.fds[0], b"L%d" % self.frame_no[0]),
            (self.fds[1], b"R%d" % self.frame_no[1]),
        ]

    def _bump(self):
        self.frame_no[0] += 1
        self.frame_no[1] += 1

    def test_writes_continue_during_delayed_release(self):
        """After the duration deadline the writer keeps feeding while the
        release is pending (fds still valid), then stops on stop()."""
        writer = bap_central_writer.PacedWriter(
            self.encode, 0.05, frame_interval_s=0.01
        )
        writer.start()
        time.sleep(0.13)  # past the 0.05 s duration, release not yet done
        # Duration-phase accounting is exact; the tail keeps flowing.
        self.assertGreaterEqual(writer.frames, 4)
        self.assertLessEqual(writer.frames, 6)
        self.assertGreaterEqual(writer.tail_frames, 1)
        self.assertIsNone(writer.error)
        self.assertFalse(writer.stopped_by_fd)
        # Release completes: stop + bounded join.
        writer.stop()
        self.assertTrue(writer.join(timeout_s=5.0))
        writes_before_stop = self.fds[0].writes
        time.sleep(0.03)
        self.assertEqual(self.fds[0].writes, writes_before_stop, "no writes after stop")
        for fd in self.fds:
            fd.drain()

    def test_no_writes_after_fds_invalid(self):
        """Closing the transport fds stops the writer on its own (the
        release path invalidates the fds), with bounded join."""
        writer = bap_central_writer.PacedWriter(self.encode, 1.0, frame_interval_s=0.01)
        writer.start()
        time.sleep(0.05)
        for fd in self.fds:
            fd.close()
        self.assertTrue(
            writer.join(timeout_s=5.0), "writer must stop when all fds are invalid"
        )
        self.assertTrue(writer.stopped_by_fd)
        self.assertIsNone(writer.error)
        writes_after_close = self.fds[0].writes
        time.sleep(0.03)
        self.assertEqual(self.fds[0].writes, writes_after_close)

    def test_error_captured_and_bounded(self):
        """An encode error stops the writer and is propagated."""

        def bad_encode():
            raise RuntimeError("encode boom")

        writer = bap_central_writer.PacedWriter(bad_encode, 0.05, frame_interval_s=0.01)
        writer.start()
        self.assertTrue(writer.join(timeout_s=5.0))
        self.assertIsNotNone(writer.error)
        self.assertEqual(str(writer.error), "encode boom")

    def test_join_bounded_when_writer_keeps_running(self):
        """A writer whose fds stay valid and is never stopped must not
        block join forever; stop() then terminates it."""
        writer = bap_central_writer.PacedWriter(self.encode, 5.0, frame_interval_s=0.01)
        writer.start()
        time.sleep(0.05)
        self.assertFalse(
            writer.join(timeout_s=0.2), "join must return False while still writing"
        )
        writer.stop()
        self.assertTrue(writer.join(timeout_s=5.0))
        for fd in self.fds:
            fd.drain()

    def test_exact_duration_accounting(self):
        """frames counts only the requested duration; a short run with an
        immediate stop yields ~duration/interval frames and zero tail."""
        writer = bap_central_writer.PacedWriter(
            self.encode, 0.05, frame_interval_s=0.01
        )
        writer.start()
        time.sleep(0.055)
        writer.stop()
        self.assertTrue(writer.join(timeout_s=5.0))
        self.assertGreaterEqual(writer.frames, 4)
        self.assertLessEqual(writer.frames, 6)
        self.assertLessEqual(writer.tail_frames, 1)
        for fd in self.fds:
            fd.drain()

    def test_empty_frame_means_teardown(self):
        """An encode result with no transports (all released) stops the
        writer the same way invalid fds do."""

        def empty_encode():
            return []

        writer = bap_central_writer.PacedWriter(
            empty_encode, 1.0, frame_interval_s=0.01
        )
        writer.start()
        self.assertTrue(writer.join(timeout_s=5.0))
        self.assertTrue(writer.stopped_by_fd)


if __name__ == "__main__":
    unittest.main()
