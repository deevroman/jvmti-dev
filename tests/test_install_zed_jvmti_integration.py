import json
import tempfile
import unittest
from pathlib import Path

from scripts.install_zed_jvmti_integration import build_tasks, install, merge_tasks


class InstallZedIntegrationTests(unittest.TestCase):
    def test_merge_tasks_replaces_managed_labels_and_preserves_other_tasks(self) -> None:
        existing = [
            {"label": "Custom task", "command": "echo", "args": ["custom"]},
            {"label": "Generate JVMTI config from selected Java method", "command": "old"},
        ]
        merged = merge_tasks(existing, build_tasks())

        self.assertEqual(merged[0]["label"], "Custom task")
        self.assertEqual(merged[1]["label"], "Generate JVMTI config from selected Java method")
        self.assertEqual(merged[1]["command"], "python3")
        self.assertEqual(merged[2]["label"], "Generate and send JVMTI config to JVM")

    def test_install_writes_bundle_and_tasks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            project_root = Path(tmp_dir)
            result = install(project_root, force_settings=False)

            tasks_path = project_root / ".zed" / "tasks.json"
            settings_path = project_root / ".zed" / "jvmti-tools" / "zed_jvmti_settings.json"
            generator_path = project_root / ".zed" / "jvmti-tools" / "zed_generate_jvmti_config.py"
            sender_path = project_root / ".zed" / "jvmti-tools" / "send_runtime_config.py"

            self.assertTrue(tasks_path.exists())
            self.assertTrue(settings_path.exists())
            self.assertTrue(generator_path.exists())
            self.assertTrue(sender_path.exists())

            tasks = json.loads(tasks_path.read_text(encoding="utf-8"))
            self.assertEqual(tasks[0]["label"], "Generate JVMTI config from selected Java method")
            self.assertEqual(tasks[1]["label"], "Generate and send JVMTI config to JVM")

            settings = json.loads(settings_path.read_text(encoding="utf-8"))
            self.assertEqual(settings["send_port"], 9009)
            self.assertIn(str(generator_path), result["copied_files"])


if __name__ == "__main__":
    unittest.main()
