import socket
import threading
import time
import unittest

from rvc_runtime_gateway import RsvcGateway, RuntimeTarget
from rvc_runtime_service import Runtime, _serve_and_close
from rvc_runtime_supervisor import probe_rsvc_stream


class RuntimeGatewayTest(unittest.TestCase):
    def setUp(self):
        self.backend = socket.create_server(("127.0.0.1", 0))
        self.backend_thread = threading.Thread(target=self._echo_once, daemon=True)
        self.backend_thread.start()
        target = RuntimeTarget(
            "local-test",
            "Local test runtime",
            "127.0.0.1",
            self.backend.getsockname()[1],
            local=True,
        )
        self.gateway = RsvcGateway(target, port=0)
        self.gateway.start()
        self.gateway.port = self.gateway._listener.getsockname()[1]

    def tearDown(self):
        self.gateway.stop()
        self.backend.close()
        self.backend_thread.join(1)

    def _echo_once(self):
        self.backend.settimeout(1.0)
        try:
            client, _address = self.backend.accept()
        except OSError:
            return
        with client:
            data = client.recv(1024)
            client.sendall(data[::-1])

    def test_forwards_bytes_without_interpreting_rsvc(self):
        with socket.create_connection(("127.0.0.1", self.gateway.port)) as client:
            client.sendall(b"RSVC-test")
            client.shutdown(socket.SHUT_WR)
            self.assertEqual(client.recv(1024), b"tset-CVSR")

    def test_selection_changes_target_and_route_generation(self):
        replacement = RuntimeTarget("chosen", "Chosen runtime", "192.0.2.1", 19000)
        generation = self.gateway.snapshot()["route_generation"]
        self.gateway.select(replacement)
        self.assertEqual(self.gateway.snapshot()["target_identity"], "chosen")
        self.assertFalse(self.gateway.snapshot()["local"])
        self.assertEqual(self.gateway.snapshot()["route_generation"], generation + 1)

    def test_selection_disconnects_existing_session_before_new_route(self):
        self.gateway.stop()
        self.backend.close()
        self.backend_thread.join(1)

        first = socket.create_server(("127.0.0.1", 0))
        second = socket.create_server(("127.0.0.1", 0))
        self.addCleanup(first.close)
        self.addCleanup(second.close)
        first_connected = threading.Event()

        def hold_first():
            client, _address = first.accept()
            with client:
                first_connected.set()
                while client.recv(1024):
                    pass

        def echo_second():
            client, _address = second.accept()
            with client:
                client.sendall(client.recv(1024) + b"-second")

        first_thread = threading.Thread(target=hold_first, daemon=True)
        second_thread = threading.Thread(target=echo_second, daemon=True)
        first_thread.start()
        second_thread.start()
        self.backend = first
        self.backend_thread = first_thread
        self.gateway = RsvcGateway(
            RuntimeTarget("first", "First", "127.0.0.1", first.getsockname()[1]),
            port=0,
        )
        self.gateway.start()
        self.gateway.port = self.gateway._listener.getsockname()[1]

        old_client = socket.create_connection(("127.0.0.1", self.gateway.port))
        old_client.settimeout(1.0)
        old_client.sendall(b"old")
        self.assertTrue(first_connected.wait(1.0))
        deadline = time.monotonic() + 1.0
        while self.gateway.snapshot()["active_sessions"] != 1 and time.monotonic() < deadline:
            time.sleep(0.01)

        self.gateway.select(
            RuntimeTarget("second", "Second", "127.0.0.1", second.getsockname()[1])
        )
        self.assertEqual(old_client.recv(1), b"")
        old_client.close()
        with socket.create_connection(("127.0.0.1", self.gateway.port)) as new_client:
            new_client.sendall(b"new")
            self.assertEqual(new_client.recv(1024), b"new-second")
        second_thread.join(1)

    def test_rsvc_handshake_reaches_backend_through_gateway(self):
        self.gateway.stop()
        self.backend.close()
        self.backend_thread.join(1)

        self.backend = socket.create_server(("127.0.0.1", 0))

        def serve_once():
            client, _address = self.backend.accept()
            _serve_and_close(client, Runtime())

        self.backend_thread = threading.Thread(target=serve_once, daemon=True)
        self.backend_thread.start()
        self.gateway = RsvcGateway(
            RuntimeTarget(
                "local-rsvc",
                "Local RSVC",
                "127.0.0.1",
                self.backend.getsockname()[1],
                local=True,
            ),
            port=0,
        )
        self.gateway.start()
        self.gateway.port = self.gateway._listener.getsockname()[1]
        result = probe_rsvc_stream(port=self.gateway.port)
        self.assertTrue(result.ready, result.detail)


if __name__ == "__main__":
    unittest.main()
