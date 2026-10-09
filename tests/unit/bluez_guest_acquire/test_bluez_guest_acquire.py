"""Real descriptor-boundary tests for pending public Acquire ownership."""

import os
from pathlib import Path
import socket
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts"))
from bluez_guest_acquire import AcquireOwner


class Pending:
    def __init__(self, error=False):
        self.error = error

    def cancel(self):
        if self.error:
            raise RuntimeError("cancel failed")


class AcquireTests(unittest.TestCase):
    def pair(self):
        return socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)

    def test_taken_descriptor_survives_owner_cleanup(self):
        owner = AcquireOwner()
        peer, local = self.pair()
        with peer, local:
            token = owner.register("/source", "source")
            owner.received(token, os.dup(local.fileno()), 0, 120)
            lease = owner.take(token)
            with self.assertRaises(ValueError):
                owner.take(token)
            self.assertEqual(owner.begin_cleanup(), [])
            self.assertEqual(owner.finish_cleanup(), [])
            try:
                os.write(lease["fd"], b"packet")
                self.assertEqual(peer.recv(32), b"packet")
            finally:
                os.close(lease["fd"])

    def test_early_cleanup_and_late_reply_close_fds(self):
        owner = AcquireOwner()
        peer, local = self.pair()
        token = owner.register("/source", "source")
        owner.received(token, os.dup(local.fileno()), 0, 120)
        local.close()
        with peer:
            self.assertEqual(len(owner.begin_cleanup()), 1)
            self.assertEqual(owner.finish_cleanup(), [])
            self.assertEqual(peer.recv(1), b"")
        late_peer, late_local = self.pair()
        with late_peer:
            fd = late_local.detach()
            with self.assertRaisesRegex(ValueError, "Late"):
                owner.received(token, fd, 0, 120)
            self.assertEqual(late_peer.recv(1), b"")
        self.assertTrue(any("Late" in error for error in owner.errors))

    def test_duplicate_reply_keeps_first_descriptor(self):
        owner = AcquireOwner()
        token = owner.register("/source", "source")
        first_peer, first = self.pair()
        second_peer, second = self.pair()
        with first_peer, first, second_peer:
            owner.received(token, os.dup(first.fileno()), 0, 120)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                owner.received(token, second.detach(), 0, 120)
            self.assertEqual(second_peer.recv(1), b"")
            lease = owner.take(token)
            try:
                os.write(lease["fd"], b"first")
                self.assertEqual(first_peer.recv(32), b"first")
            finally:
                os.close(lease["fd"])
            owner.finish_cleanup()

    def test_second_owner_failure_closes_first_and_cancel_error_does_not_block(self):
        owner = AcquireOwner()
        sink = owner.register("/sink", "sink")
        source = owner.register("/source", "source")
        peer, local = self.pair()
        with peer:
            owner.received(source, local.detach(), 0, 120)
            owner.bind(source, Pending(error=True))
            owner.bind(sink, Pending())
            owner.failed(sink, RuntimeError("sink unavailable"))
            self.assertEqual(
                [item[1] for item in owner.begin_cleanup()], ["/source", "/sink"]
            )
            self.assertTrue(
                any("cancel failed" in item for item in owner.finish_cleanup())
            )
            self.assertEqual(peer.recv(1), b"")

    def test_unknown_token_closes_transferred_descriptor(self):
        owner = AcquireOwner()
        unknown_peer, unknown_local = self.pair()
        with unknown_peer:
            with self.assertRaisesRegex(ValueError, "Unknown Acquire reply token"):
                owner.received(object(), unknown_local.detach(), 0, 120)
            self.assertEqual(unknown_peer.recv(1), b"")

    def test_repeated_finish_does_not_close_reused_descriptor(self):
        owner = AcquireOwner()
        token = owner.register("/source", "source")
        peer, local = self.pair()
        with peer:
            old_fd = local.detach()
            owner.received(token, old_fd, 0, 120)
            owner.begin_cleanup()
            owner.finish_cleanup()
            self.assertEqual(peer.recv(1), b"")
        new_peer, new_local = self.pair()
        with new_peer, new_local:
            os.dup2(new_local.fileno(), old_fd)
            try:
                self.assertEqual(owner.finish_cleanup(), [])
                os.write(old_fd, b"alive")
                self.assertEqual(new_peer.recv(32), b"alive")
            finally:
                if old_fd != new_local.fileno():
                    os.close(old_fd)

    def test_registration_validation_and_unready_take(self):
        owner = AcquireOwner()
        token = owner.register("/source", "source")
        with self.assertRaises(ValueError):
            owner.take(token)
        with self.assertRaises(ValueError):
            owner.register("/source", "sink")
        with self.assertRaises(ValueError):
            owner.register("/other", "source")
        owner.begin_cleanup()
        with self.assertRaises(ValueError):
            owner.register("/sink", "sink")

    def test_invalid_mtu_closes_delivered_descriptor(self):
        owner = AcquireOwner()
        token = owner.register("/source", "source")
        peer, local = self.pair()
        with peer:
            with self.assertRaisesRegex(ValueError, "Invalid Acquire fd or MTU"):
                owner.received(token, local.detach(), True, 120)
            self.assertEqual(peer.recv(1), b"")
        with self.assertRaises(ValueError):
            owner.take(token)
        owner.finish_cleanup()


if __name__ == "__main__":
    unittest.main()
