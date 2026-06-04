#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


BUNDLE_DIRNAME = "jvmti-tools"
BUNDLE_FILES = (
    "send_runtime_config.py",
    "zed_generate_jvmti_config.py",
)
TASK_LABELS = (
    "Generate JVMTI config from selected Java method",
    "Generate and send JVMTI config to JVM",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Install reusable Zed JVMTI tasks into any project."
    )
    parser.add_argument("--project", required=True, help="Target project root")
    parser.add_argument(
        "--force-settings",
        action="store_true",
        help="Overwrite .zed/jvmti-tools/zed_jvmti_settings.json if it already exists",
    )
    return parser.parse_args()


def build_tasks() -> list[dict[str, object]]:
    script_path = "$ZED_WORKTREE_ROOT/.zed/jvmti-tools/zed_generate_jvmti_config.py"
    common = {
        "command": "python3",
        "save": "current",
        "show_command": True,
        "show_summary": True,
        "tags": ["java", "jvmti", "tests"],
    }
    return [
        {
            **common,
            "label": "Generate JVMTI config from selected Java method",
            "args": [
                script_path,
                "--from-zed-env",
                "--write-default",
            ],
        },
        {
            **common,
            "label": "Generate and send JVMTI config to JVM",
            "args": [
                script_path,
                "--from-zed-env",
                "--write-default",
                "--send",
            ],
        },
    ]


def load_tasks(tasks_path: Path) -> list[dict[str, object]]:
    if not tasks_path.exists():
        return []
    data = json.loads(tasks_path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise ValueError(f"{tasks_path} must contain a JSON array")
    return data


def merge_tasks(existing: list[dict[str, object]], desired: list[dict[str, object]]) -> list[dict[str, object]]:
    desired_by_label = {task["label"]: task for task in desired}
    merged: list[dict[str, object]] = []
    seen_labels: set[str] = set()

    for task in existing:
        label = task.get("label")
        if label in desired_by_label:
            merged.append(desired_by_label[label])
            seen_labels.add(label)
        else:
            merged.append(task)

    for task in desired:
        label = task["label"]
        if label not in seen_labels:
            merged.append(task)

    return merged


def install_bundle_files(target_bundle_dir: Path, source_scripts_dir: Path) -> list[Path]:
    target_bundle_dir.mkdir(parents=True, exist_ok=True)
    copied: list[Path] = []
    for filename in BUNDLE_FILES:
        source = source_scripts_dir / filename
        target = target_bundle_dir / filename
        shutil.copy2(source, target)
        copied.append(target)
    return copied


def write_settings_file(settings_path: Path, force: bool) -> None:
    if settings_path.exists() and not force:
        return
    settings = {
        "send_host": "127.0.0.1",
        "send_port": 9009,
        "send_timeout": 5.0,
    }
    settings_path.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")


def install(project_root: Path, force_settings: bool) -> dict[str, object]:
    zed_dir = project_root / ".zed"
    bundle_dir = zed_dir / BUNDLE_DIRNAME
    tasks_path = zed_dir / "tasks.json"
    settings_path = bundle_dir / "zed_jvmti_settings.json"

    copied_files = install_bundle_files(bundle_dir, Path(__file__).resolve().parent)
    write_settings_file(settings_path, force_settings)

    merged_tasks = merge_tasks(load_tasks(tasks_path), build_tasks())
    zed_dir.mkdir(parents=True, exist_ok=True)
    tasks_path.write_text(json.dumps(merged_tasks, indent=2) + "\n", encoding="utf-8")

    return {
        "project_root": str(project_root),
        "tasks_path": str(tasks_path),
        "settings_path": str(settings_path),
        "copied_files": [str(path) for path in copied_files],
        "task_labels": list(TASK_LABELS),
    }


def main() -> int:
    args = parse_args()
    project_root = Path(args.project).resolve()
    result = install(project_root, args.force_settings)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
