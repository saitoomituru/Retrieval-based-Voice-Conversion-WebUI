import socket
import threading
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

    def test_selection_changes_only_new_session_target(self):
        replacement = RuntimeTarget("chosen", "Chosen runtime", "192.0.2.1", 19000)
        self.gateway.select(replacement)
        self.assertEqual(self.gateway.snapshot()["target_identity"], "chosen")
        self.assertFalse(self.gateway.snapshot()["local"])

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
