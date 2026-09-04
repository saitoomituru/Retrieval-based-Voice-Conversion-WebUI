import socket
import tempfile
import threading
import unittest
from pathlib import Path

from rvc_runtime_service import Runtime, _serve_and_close
from rvc_runtime_supervisor import ProbeResult, RvcRuntimeSupervisor, probe_rsvc_stream


class FakeProcess:
    def __init__(self, pid=1234):
        self.pid = pid
        self.returncode = None
        self.terminated = False

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated = True
        self.returncode = 0

    def wait(self, timeout=None):
        return self.returncode

    def kill(self):
        self.returncode = -9


class RuntimeSupervisorTest(unittest.TestCase):
    def test_probe_uses_real_rsvc_handshake(self):
        listener = socket.create_server(("127.0.0.1", 0))
        self.addCleanup(listener.close)

        def accept_once():
            client, _address = listener.accept()
            _serve_and_close(client, Runtime())

        worker = threading.Thread(target=accept_once, daemon=True)
        worker.start()
        result = probe_rsvc_stream(port=listener.getsockname()[1])
        self.assertTrue(result.ready, result.detail)
        worker.join(1)

    def test_reuses_compatible_existing_runner_without_spawn(self):
        spawned = []
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "rvc_runtime_service.py").write_text("", encoding="utf-8")
            supervisor = RvcRuntimeSupervisor(
                root,
                probe=lambda *_args, **_kwargs: ProbeResult("ready", "existing"),
                popen=lambda *args, **kwargs: spawned.append((args, kwargs)),
            )
            supervisor.ensure_running()
            self.assertEqual(supervisor.snapshot()["state"], "REUSED")
            self.assertEqual(spawned, [])

    def test_starts_and_stops_only_owned_runner(self):
        results = iter([
            ProbeResult("unavailable", "missing"),
            ProbeResult("ready", "owned"),
        ])
        process = FakeProcess()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "rvc_runtime_service.py").write_text("", encoding="utf-8")
            supervisor = RvcRuntimeSupervisor(
                root,
                probe=lambda *_args, **_kwargs: next(results),
                popen=lambda *args, **kwargs: process,
                monitor_interval=10,
            )
            supervisor.ensure_running()
            self.assertEqual(supervisor.snapshot()["state"], "STARTING")
            supervisor.ensure_running()
            self.assertEqual(supervisor.snapshot()["state"], "READY")
            supervisor.stop()
            self.assertTrue(process.terminated)

    def test_incompatible_listener_is_never_replaced(self):
        spawned = []
        with tempfile.TemporaryDirectory() as tmp:
            supervisor = RvcRuntimeSupervisor(
                Path(tmp),
                probe=lambda *_args, **_kwargs: ProbeResult("incompatible", "wrong protocol"),
                popen=lambda *args, **kwargs: spawned.append((args, kwargs)),
            )
            supervisor.ensure_running()
            self.assertEqual(supervisor.snapshot()["state"], "ERROR")
            self.assertEqual(spawned, [])

    def test_engine_configuration_restarts_only_owned_runner(self):
        probes = iter([
            ProbeResult("unavailable", "missing"),
            ProbeResult("unavailable", "restart"),
            ProbeResult("unavailable", "starting configured runner"),
        ])
        processes = [FakeProcess(1001), FakeProcess(1002)]
        commands = []
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "rvc_runtime_service.py").write_text("", encoding="utf-8")
            config = root / "engine.json"
            config.write_text("{}", encoding="utf-8")

            def spawn(command, **_kwargs):
                commands.append(command)
                return processes[len(commands) - 1]

            supervisor = RvcRuntimeSupervisor(
                root,
                probe=lambda *_args, **_kwargs: next(probes),
                popen=spawn,
            )
            supervisor.ensure_running()
            self.assertTrue(supervisor.configure_engine(config))
            self.assertTrue(processes[0].terminated)
            self.assertIn("--engine-config", commands[1])
            self.assertEqual(supervisor.snapshot()["pid"], 1002)
            supervisor.stop()

    def test_persisted_engine_configuration_is_used_on_initial_spawn(self):
        commands = []
        process = FakeProcess()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "rvc_runtime_service.py").write_text("", encoding="utf-8")
            config = root / "engine.json"
            config.write_text("{}", encoding="utf-8")

            def spawn(command, **_kwargs):
                commands.append(command)
                return process

            supervisor = RvcRuntimeSupervisor(
                root,
                engine_config=config,
                probe=lambda *_args, **_kwargs: ProbeResult("unavailable", "missing"),
                popen=spawn,
            )
            supervisor.ensure_running()
            self.assertIn("--engine-config", commands[0])
            self.assertEqual(commands[0][-1], str(config.resolve()))
            self.assertEqual(supervisor.snapshot()["engine"], "rvc")
            supervisor.stop()


if __name__ == "__main__":
    unittest.main()
