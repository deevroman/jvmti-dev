import unittest
from pathlib import Path
from unittest import mock

from scripts.zed_generate_jvmti_config import generate_config, run_send_runtime_config


class GenerateConfigTests(unittest.TestCase):
    def test_simple_method_in_default_package(self) -> None:
        source = """\
public class Calculator {

    public int add(int a, int b) {
        return a + b;
    }
}
"""
        selection = """\
    public int add(int a, int b) {
        return a + b;
    }
"""
        self.assertEqual(
            generate_config(source, selection),
            {
                "target_class": "Calculator",
                "target_method": "add",
                "target_method_signature": "(II)I",
                "dump": "dump_calculator_add.json",
            },
        )

    def test_package_imports_and_varargs(self) -> None:
        source = """\
package com.example.tools;

import java.util.List;

public class Formatter {

    public List<String> normalize(String... values) {
        return java.util.Arrays.asList(values);
    }
}
"""
        selection = """\
    public List<String> normalize(String... values) {
        return java.util.Arrays.asList(values);
    }
"""
        self.assertEqual(
            generate_config(source, selection),
            {
                "target_class": "com.example.tools.Formatter",
                "target_method": "normalize",
                "target_method_signature": "([Ljava/lang/String;)Ljava/util/List;",
                "dump": "dump_com_example_tools_formatter_normalize.json",
            },
        )

    def test_nested_class_constructor(self) -> None:
        source = """\
package sample;

public class Outer {
    static class Inner {
        Inner(int[] values) {
        }
    }
}
"""
        selection = """\
        Inner(int[] values) {
        }
"""
        self.assertEqual(
            generate_config(source, selection),
            {
                "target_class": "sample.Outer$Inner",
                "target_method": "<init>",
                "target_method_signature": "([I)V",
                "dump": "dump_sample_outer_inner_init.json",
            },
        )

    def test_run_send_runtime_config_invokes_existing_sender(self) -> None:
        with mock.patch("scripts.zed_generate_jvmti_config.subprocess.run") as run_mock:
            run_mock.return_value = mock.Mock(returncode=0, stdout="OK\n", stderr="")
            result = run_send_runtime_config(
                config_path=Path("/tmp/config.json"),
                send_script=Path("/repo/scripts/send_runtime_config.py"),
                host="127.0.0.1",
                port=9009,
                timeout=5.0,
            )

        self.assertEqual(result.returncode, 0)
        run_mock.assert_called_once_with(
            [
                mock.ANY,
                "/repo/scripts/send_runtime_config.py",
                "--host",
                "127.0.0.1",
                "--port",
                "9009",
                "--timeout",
                "5.0",
                "--payload-file",
                "/tmp/config.json",
            ],
            check=False,
            text=True,
            capture_output=True,
        )


if __name__ == "__main__":
    unittest.main()
