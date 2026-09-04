import json
import urllib.request
import unittest
from pathlib import Path

from rvc_runtime_bonjour import BonjourRuntimeDirectory, LOCAL_CHOICE
from rvc_runtime_control import RuntimeRouterControl
from rvc_runtime_gateway import RsvcGateway


class RuntimeRouterControlTest(unittest.TestCase):
    def setUp(self):
        self.directory = BonjourRuntimeDirectory(Path("."), 17866)
        self.gateway = RsvcGateway(self.directory.local_target(), port=0)
        self.control = RuntimeRouterControl(self.directory, self.gateway, port=0)
        self.control.start()

    def tearDown(self):
        self.control.stop()

    def request(self, path, payload=None):
        data = None
        headers = {}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        with urllib.request.urlopen(
            urllib.request.Request(
                f"http://127.0.0.1:{self.control.port}{path}", data=data, headers=headers
            ),
            timeout=1,
        ) as response:
            return json.load(response)

    def test_lists_localhost_for_au_gui(self):
        payload = self.request("/v1/runtimes")
        self.assertEqual(payload["protocol"], 1)
        self.assertEqual(payload["selected"], LOCAL_CHOICE)
        self.assertEqual(payload["choices"], [LOCAL_CHOICE])

    def test_select_updates_shared_gateway_state(self):
        payload = self.request("/v1/select", {"choice": LOCAL_CHOICE})
        self.assertEqual(payload["selected"], LOCAL_CHOICE)
        self.assertEqual(payload["gateway"]["target"], "127.0.0.1:17866")
        self.assertTrue(payload["gateway"]["local"])


if __name__ == "__main__":
    unittest.main()
