import unittest
from pathlib import Path

from rvc_runtime_bonjour import (
    BonjourRuntimeDirectory,
    LOCAL_CHOICE,
    parse_browse_line,
    parse_resolve_output,
)


class BonjourRuntimeDirectoryTest(unittest.TestCase):
    def test_parses_add_and_remove_without_fixed_ip(self):
        added = parse_browse_line(
            "10:30:00.000  Add  2  14 local. _rvc-realtime._tcp. RVC WebUI studio abc123"
        )
        removed = parse_browse_line(
            "10:31:00.000  Rmv  0  14 local. _rvc-realtime._tcp. RVC WebUI studio abc123"
        )
        self.assertEqual(added[0], "Add")
        self.assertEqual(added[1].name, "RVC WebUI studio abc123")
        self.assertEqual(added[1].identity, "RVC WebUI studio abc123._rvc-realtime._tcp.local.")
        self.assertEqual(removed[0], "Rmv")

    def test_parses_resolved_host_and_port(self):
        output = (
            "RVC WebUI studio can be reached at studio.local.:17866 "
            "(interface 14)\n proto=1 backend=cpu"
        )
        self.assertEqual(parse_resolve_output(output), ("studio.local", 17866))

    def test_advertisement_contains_only_minimal_non_secret_txt(self):
        directory = BonjourRuntimeDirectory(Path("."), 17866)
        command = directory.advertise_command()
        self.assertEqual(command[:6], [
            "/usr/bin/dns-sd", "-R", directory.service_name,
            "_rvc-realtime._tcp", "local", "17866",
        ])
        self.assertIn("proto=1", command)
        self.assertIn("backend=cpu", command)
        self.assertNotIn(str(Path(".").resolve()), " ".join(command))

    def test_self_selection_routes_to_loopback_backend(self):
        directory = BonjourRuntimeDirectory(Path("."), 17866)
        target = directory.resolve(LOCAL_CHOICE)
        self.assertTrue(target.local)
        self.assertEqual((target.host, target.port), ("127.0.0.1", 17866))


if __name__ == "__main__":
    unittest.main()
