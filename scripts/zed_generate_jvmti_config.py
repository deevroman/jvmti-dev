#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_ZED_SETTINGS_FILENAME = "zed_jvmti_settings.json"


JAVA_LANG_TYPES = {
    "Boolean",
    "Byte",
    "Character",
    "Class",
    "Double",
    "Enum",
    "Exception",
    "Float",
    "Integer",
    "Iterable",
    "Long",
    "Math",
    "Number",
    "Object",
    "Override",
    "Runnable",
    "RuntimeException",
    "Short",
    "String",
    "StringBuilder",
    "StringBuffer",
    "System",
    "Throwable",
    "Void",
}

PRIMITIVE_DESCRIPTORS = {
    "boolean": "Z",
    "byte": "B",
    "char": "C",
    "double": "D",
    "float": "F",
    "int": "I",
    "long": "J",
    "short": "S",
    "void": "V",
}

MODIFIERS = {
    "public",
    "protected",
    "private",
    "static",
    "final",
    "abstract",
    "synchronized",
    "native",
    "strictfp",
    "default",
    "transient",
    "volatile",
    "sealed",
    "non-sealed",
}


@dataclass(frozen=True)
class ClassScope:
    name: str
    start: int
    body_start: int
    end: int


@dataclass(frozen=True)
class ParsedMethod:
    target_method: str
    method_signature: str


def strip_comments_and_literals(source: str) -> str:
    chars = list(source)
    i = 0
    state = "code"
    while i < len(chars):
        ch = chars[i]
        nxt = chars[i + 1] if i + 1 < len(chars) else ""

        if state == "code":
            if ch == "/" and nxt == "/":
                chars[i] = " "
                chars[i + 1] = " "
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                chars[i] = " "
                chars[i + 1] = " "
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                chars[i] = " "
                i += 1
                state = "string"
                continue
            if ch == "'":
                chars[i] = " "
                i += 1
                state = "char"
                continue
            i += 1
            continue

        if state == "line_comment":
            if ch != "\n":
                chars[i] = " "
            else:
                state = "code"
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                chars[i] = " "
                chars[i + 1] = " "
                i += 2
                state = "code"
                continue
            if ch != "\n":
                chars[i] = " "
            i += 1
            continue

        if state in {"string", "char"}:
            if ch == "\\":
                chars[i] = " "
                if i + 1 < len(chars):
                    chars[i + 1] = " "
                i += 2
                continue
            if ch == "\n":
                state = "code"
                i += 1
                continue
            chars[i] = " "
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "code"
            i += 1

    return "".join(chars)


def find_matching_delimiter(source: str, open_index: int, open_char: str, close_char: str) -> int:
    depth = 0
    for index in range(open_index, len(source)):
        current = source[index]
        if current == open_char:
            depth += 1
        elif current == close_char:
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(f"Unmatched delimiter {open_char} at position {open_index}")


def collect_class_scopes(clean_source: str) -> list[ClassScope]:
    scopes: list[ClassScope] = []
    pattern = re.compile(r"\b(class|interface|enum|record)\s+([A-Za-z_]\w*)\b")
    for match in pattern.finditer(clean_source):
        brace_index = clean_source.find("{", match.end())
        if brace_index == -1:
            continue
        try:
            end_index = find_matching_delimiter(clean_source, brace_index, "{", "}")
        except ValueError:
            continue
        scopes.append(
            ClassScope(
                name=match.group(2),
                start=match.start(),
                body_start=brace_index,
                end=end_index,
            )
        )
    scopes.sort(key=lambda scope: (scope.start, scope.end))
    return scopes


def build_binary_name_maps(scopes: Iterable[ClassScope], package_name: str) -> tuple[dict[str, str], dict[str, str]]:
    scopes = sorted(scopes, key=lambda scope: (scope.start, scope.end))
    chain_for_scope: dict[ClassScope, list[ClassScope]] = {}
    qualified_by_source: dict[str, str] = {}
    binary_by_simple: dict[str, str] = {}

    for scope in scopes:
        parents = [
            other
            for other in scopes
            if other.start < scope.start and other.end > scope.end
        ]
        parents.sort(key=lambda item: item.start)
        chain = parents + [scope]
        chain_for_scope[scope] = chain
        source_name = ".".join(item.name for item in chain)
        binary_tail = "$".join(item.name for item in chain)
        binary_name = f"{package_name}.{binary_tail}" if package_name else binary_tail
        qualified_by_source[source_name] = binary_name
        binary_by_simple.setdefault(scope.name, binary_name)

    return qualified_by_source, binary_by_simple


def extract_package(source: str) -> str:
    match = re.search(r"(?m)^\s*package\s+([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*;", source)
    return match.group(1) if match else ""


def extract_imports(source: str) -> tuple[dict[str, str], list[str]]:
    explicit: dict[str, str] = {}
    wildcards: list[str] = []
    pattern = re.compile(r"(?m)^\s*import\s+(static\s+)?([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*(?:\.\*)?)\s*;")
    for match in pattern.finditer(source):
        if match.group(1):
            continue
        imported = match.group(2)
        if imported.endswith(".*"):
            wildcards.append(imported[:-2])
            continue
        explicit[imported.split(".")[-1]] = imported
    return explicit, wildcards


def strip_leading_annotations(text: str) -> str:
    stripped = text.lstrip()
    while stripped.startswith("@"):
        paren_depth = 0
        consumed = 0
        while consumed < len(stripped):
            ch = stripped[consumed]
            consumed += 1
            if ch == "(":
                paren_depth += 1
            elif ch == ")":
                if paren_depth > 0:
                    paren_depth -= 1
            elif ch in "\r\n" and paren_depth == 0:
                break
            elif paren_depth == 0 and ch.isspace():
                while consumed < len(stripped) and stripped[consumed].isspace():
                    consumed += 1
                break
        stripped = stripped[consumed:].lstrip()
    return stripped


def erase_generics(text: str) -> str:
    result: list[str] = []
    depth = 0
    for ch in text:
        if ch == "<":
            depth += 1
            continue
        if ch == ">":
            if depth > 0:
                depth -= 1
            continue
        if depth == 0:
            result.append(ch)
    return "".join(result)


def split_top_level(text: str, delimiter: str) -> list[str]:
    items: list[str] = []
    depth = 0
    current: list[str] = []
    for ch in text:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            if depth > 0:
                depth -= 1
        if ch == delimiter and depth == 0:
            items.append("".join(current).strip())
            current = []
            continue
        current.append(ch)
    tail = "".join(current).strip()
    if tail:
        items.append(tail)
    return items


def split_method_header(selection: str) -> tuple[str, str]:
    clean_selection = strip_comments_and_literals(selection)
    body_start = clean_selection.find("{")
    if body_start == -1:
        raise ValueError("The selection does not look like a concrete Java method")
    header = selection[:body_start].strip()
    return header, strip_comments_and_literals(header)


def strip_leading_type_parameters(text: str) -> str:
    candidate = text.lstrip()
    if not candidate.startswith("<"):
        return candidate
    depth = 0
    for index, ch in enumerate(candidate):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                return candidate[index + 1 :].lstrip()
    return candidate


def parse_method_name_and_return_type(prefix: str, class_simple_name: str) -> tuple[str, str | None]:
    prefix = strip_leading_annotations(prefix)
    prefix = re.sub(r"\s+", " ", prefix).strip()
    name_match = re.search(r"([A-Za-z_]\w*)\s*$", prefix)
    if not name_match:
        raise ValueError("Failed to extract method name from the selection")
    method_name = name_match.group(1)
    before_name = prefix[: name_match.start()].strip()
    tokens = [token for token in before_name.split(" ") if token]
    while tokens and tokens[0] in MODIFIERS:
        tokens.pop(0)
    without_modifiers = " ".join(tokens)
    without_type_params = strip_leading_type_parameters(without_modifiers)
    return_type = without_type_params.strip() or None
    if method_name == class_simple_name and return_type is None:
        return "<init>", None
    if return_type is None:
        raise ValueError("Failed to extract the return type from the selection")
    return method_name, return_type


def parse_parameter_types(parameters_text: str) -> list[str]:
    if not parameters_text.strip():
        return []
    parameters = split_top_level(parameters_text, ",")
    return [extract_parameter_type(parameter) for parameter in parameters]


def extract_parameter_type(parameter: str) -> str:
    stripped = strip_leading_annotations(parameter.strip())
    stripped = re.sub(r"\bfinal\b", " ", stripped)
    stripped = re.sub(r"\s+", " ", stripped).strip()
    stripped = erase_generics(stripped)
    varargs = "..." in stripped
    stripped = stripped.replace("...", "[]")

    match = re.match(r"^(?P<type>.+?)\s+(?P<name>[A-Za-z_]\w*)\s*(?P<post>(?:\[\]\s*)*)$", stripped)
    if not match:
        raise ValueError(f"Failed to parse parameter declaration: {parameter}")
    type_part = match.group("type").strip()
    post_arrays = match.group("post").replace(" ", "")
    if varargs and not type_part.endswith("[]"):
        type_part = f"{type_part}[]"
    if post_arrays:
        type_part += post_arrays
    return type_part


def resolve_type_descriptor(
    source_type: str,
    current_package: str,
    explicit_imports: dict[str, str],
    wildcard_imports: list[str],
    source_binary_names: dict[str, str],
    simple_binary_names: dict[str, str],
) -> str:
    normalized = erase_generics(source_type).strip()
    dimensions = 0
    while normalized.endswith("[]"):
        dimensions += 1
        normalized = normalized[:-2].strip()

    primitive = PRIMITIVE_DESCRIPTORS.get(normalized)
    if primitive:
        return ("[" * dimensions) + primitive

    binary_name = resolve_reference_type(
        normalized,
        current_package=current_package,
        explicit_imports=explicit_imports,
        wildcard_imports=wildcard_imports,
        source_binary_names=source_binary_names,
        simple_binary_names=simple_binary_names,
    )
    return ("[" * dimensions) + f"L{binary_name.replace('.', '/')};"


def resolve_reference_type(
    source_type: str,
    current_package: str,
    explicit_imports: dict[str, str],
    wildcard_imports: list[str],
    source_binary_names: dict[str, str],
    simple_binary_names: dict[str, str],
) -> str:
    if source_type in source_binary_names:
        return source_binary_names[source_type]

    if source_type in simple_binary_names:
        return simple_binary_names[source_type]

    if source_type in explicit_imports:
        return explicit_imports[source_type]

    if "." in source_type:
        head, tail = source_type.split(".", 1)
        if head in explicit_imports:
            return explicit_imports[head] + "$" + tail.replace(".", "$")
        if head in simple_binary_names:
            return simple_binary_names[head] + "$" + tail.replace(".", "$")
        if is_fully_qualified_reference(source_type):
            return convert_qualified_source_type(source_type)

    if source_type in JAVA_LANG_TYPES:
        return f"java.lang.{source_type}"

    for wildcard in wildcard_imports:
        if wildcard == "java.lang":
            return f"java.lang.{source_type}"

    if current_package:
        return f"{current_package}.{source_type}"
    return source_type


def is_fully_qualified_reference(source_type: str) -> bool:
    return "." in source_type and source_type.split(".")[0][:1].islower()


def convert_qualified_source_type(source_type: str) -> str:
    parts = source_type.split(".")
    class_start = None
    for index, part in enumerate(parts):
        if part[:1].isupper():
            class_start = index
            break
    if class_start is None:
        return source_type
    package_name = ".".join(parts[:class_start])
    class_name = "$".join(parts[class_start:])
    return f"{package_name}.{class_name}" if package_name else class_name


def parse_selected_method(
    selection: str,
    class_simple_name: str,
    package_name: str,
    explicit_imports: dict[str, str],
    wildcard_imports: list[str],
    source_binary_names: dict[str, str],
    simple_binary_names: dict[str, str],
) -> ParsedMethod:
    header, clean_header = split_method_header(selection)
    paren_start = clean_header.find("(")
    if paren_start == -1:
        raise ValueError("Failed to locate parameter list in the selected method")
    paren_end = find_matching_delimiter(clean_header, paren_start, "(", ")")

    prefix = header[:paren_start].strip()
    parameters_text = header[paren_start + 1 : paren_end]

    method_name, return_type = parse_method_name_and_return_type(prefix, class_simple_name)
    parameter_types = parse_parameter_types(parameters_text)
    parameters_descriptor = "".join(
        resolve_type_descriptor(
            parameter_type,
            current_package=package_name,
            explicit_imports=explicit_imports,
            wildcard_imports=wildcard_imports,
            source_binary_names=source_binary_names,
            simple_binary_names=simple_binary_names,
        )
        for parameter_type in parameter_types
    )

    return_descriptor = "V"
    if return_type is not None:
        return_descriptor = resolve_type_descriptor(
            return_type,
            current_package=package_name,
            explicit_imports=explicit_imports,
            wildcard_imports=wildcard_imports,
            source_binary_names=source_binary_names,
            simple_binary_names=simple_binary_names,
        )

    return ParsedMethod(
        target_method=method_name,
        method_signature=f"({parameters_descriptor}){return_descriptor}",
    )


def offset_from_row_column(source: str, row: int | None, column: int | None) -> int | None:
    if row is None or column is None:
        return None
    if row < 1 or column < 1:
        return None
    lines = source.splitlines(keepends=True)
    if row > len(lines):
        return None
    return sum(len(line) for line in lines[: row - 1]) + min(column - 1, len(lines[row - 1]))


def locate_selection_offset(source: str, selection: str, row: int | None, column: int | None) -> int:
    if not selection.strip():
        raise ValueError("ZED_SELECTED_TEXT is empty; select a full Java method first")

    matches: list[int] = []
    start = 0
    while True:
        found = source.find(selection, start)
        if found == -1:
            break
        matches.append(found)
        start = found + 1

    if not matches:
        raise ValueError("The exact selection was not found in the file; reselect the method and try again")
    if len(matches) == 1:
        return matches[0]

    cursor_offset = offset_from_row_column(source, row, column)
    if cursor_offset is not None:
        return min(matches, key=lambda item: abs(item - cursor_offset))
    return matches[0]


def find_enclosing_class(scopes: Iterable[ClassScope], offset: int) -> list[ClassScope]:
    chain = [scope for scope in scopes if scope.body_start < offset < scope.end]
    chain.sort(key=lambda scope: scope.start)
    if not chain:
        raise ValueError("The selection is not inside a Java class")
    return chain


def default_dump_name(target_class: str, target_method: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9]+", "_", f"{target_class}_{target_method}").strip("_").lower()
    return f"dump_{slug}.json"


def default_output_path(worktree_root: Path, target_class: str, target_method: str) -> Path:
    slug = re.sub(r"[^A-Za-z0-9]+", "_", f"{target_class}_{target_method}").strip("_").lower()
    return worktree_root / ".artifacts" / "zed-configs" / f"config_{slug}.json"


def generate_config(
    source: str,
    selection: str,
    row: int | None = None,
    column: int | None = None,
) -> dict[str, str]:
    clean_source = strip_comments_and_literals(source)
    package_name = extract_package(source)
    explicit_imports, wildcard_imports = extract_imports(source)
    scopes = collect_class_scopes(clean_source)
    source_binary_names, simple_binary_names = build_binary_name_maps(scopes, package_name)

    selection_offset = locate_selection_offset(source, selection, row, column)
    class_chain = find_enclosing_class(scopes, selection_offset)
    target_class = source_binary_names[".".join(scope.name for scope in class_chain)]
    parsed_method = parse_selected_method(
        selection=selection,
        class_simple_name=class_chain[-1].name,
        package_name=package_name,
        explicit_imports=explicit_imports,
        wildcard_imports=wildcard_imports,
        source_binary_names=source_binary_names,
        simple_binary_names=simple_binary_names,
    )

    return {
        "target_class": target_class,
        "target_method": parsed_method.target_method,
        "target_method_signature": parsed_method.method_signature,
        "dump": default_dump_name(target_class, parsed_method.target_method),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a JVMTI runtime config JSON from a selected Java method in Zed."
    )
    parser.add_argument("--file", help="Path to the Java source file")
    parser.add_argument("--selected-text", help="Exact text selected in Zed")
    parser.add_argument("--row", type=int, help="1-based cursor row from Zed")
    parser.add_argument("--column", type=int, help="1-based cursor column from Zed")
    parser.add_argument("--worktree-root", help="Repository root used to compute the default output file")
    parser.add_argument("--output-file", help="Write the generated JSON to the given path")
    parser.add_argument(
        "--from-zed-env",
        action="store_true",
        help="Read file path, selection, cursor position, and worktree root from Zed task environment variables",
    )
    parser.add_argument(
        "--write-default",
        action="store_true",
        help="Write to .artifacts/zed-configs/config_<class>_<method>.json under the worktree root",
    )
    parser.add_argument(
        "--send",
        action="store_true",
        help="Invoke scripts/send_runtime_config.py after generating the config",
    )
    parser.add_argument(
        "--send-script",
        help="Path to send_runtime_config.py; defaults to a sibling file next to this script",
    )
    parser.add_argument("--send-host", help="Socket host for send_runtime_config.py")
    parser.add_argument("--send-port", type=int, help="Socket port for send_runtime_config.py")
    parser.add_argument(
        "--send-timeout",
        type=float,
        help="Socket timeout in seconds for send_runtime_config.py",
    )
    return parser.parse_args()


def value_from_args_or_env(value: str | None, env_name: str) -> str | None:
    return value if value is not None else os.environ.get(env_name)


def int_from_args_or_env(value: int | None, env_name: str) -> int | None:
    if value is not None:
        return value
    raw = os.environ.get(env_name)
    if raw is None or not raw.strip():
        return None
    return int(raw)


def resolve_worktree_root(worktree_root: str | None, source_path: Path) -> Path:
    return Path(worktree_root) if worktree_root else source_path.parent


def default_send_script_path() -> Path:
    return SCRIPT_DIR / "send_runtime_config.py"


def default_zed_settings_path() -> Path:
    return SCRIPT_DIR / DEFAULT_ZED_SETTINGS_FILENAME


def load_zed_settings() -> dict[str, object]:
    settings_path = default_zed_settings_path()
    if not settings_path.exists():
        return {}
    return json.loads(settings_path.read_text(encoding="utf-8"))


def resolve_send_option(
    cli_value: str | int | float | None,
    settings: dict[str, object],
    settings_key: str,
    fallback: str | int | float,
) -> str | int | float:
    if cli_value is not None:
        return cli_value
    settings_value = settings.get(settings_key)
    if settings_value is not None:
        return settings_value
    return fallback


def run_send_runtime_config(
    config_path: Path,
    send_script: Path,
    host: str,
    port: int,
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        str(send_script),
        "--host",
        host,
        "--port",
        str(port),
        "--timeout",
        str(timeout),
        "--payload-file",
        str(config_path),
    ]
    return subprocess.run(command, check=False, text=True, capture_output=True)


def main() -> int:
    args = parse_args()
    source_file = value_from_args_or_env(args.file, "ZED_FILE" if args.from_zed_env else "")
    if not source_file:
        raise ValueError("Source file is required; pass --file or use --from-zed-env inside a Zed task")

    selection = value_from_args_or_env(args.selected_text, "ZED_SELECTED_TEXT" if args.from_zed_env else "")
    if selection is None:
        raise ValueError("Selected method text is required; pass --selected-text or use --from-zed-env")

    row = int_from_args_or_env(args.row, "ZED_ROW" if args.from_zed_env else "")
    column = int_from_args_or_env(args.column, "ZED_COLUMN" if args.from_zed_env else "")
    worktree_root = value_from_args_or_env(args.worktree_root, "ZED_WORKTREE_ROOT" if args.from_zed_env else "")
    worktree_root_path = resolve_worktree_root(worktree_root, Path(source_file))

    source_path = Path(source_file)
    source = source_path.read_text(encoding="utf-8")

    config = generate_config(
        source=source,
        selection=selection,
        row=row,
        column=column,
    )

    output_path: Path | None = None
    if args.output_file:
        output_path = Path(args.output_file)
    elif args.write_default:
        output_path = default_output_path(worktree_root_path, config["target_class"], config["target_method"])

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(config, indent=2))
        print()
        print(f"Wrote config to {output_path}")
        if args.send:
            zed_settings = load_zed_settings()
            send_script = Path(args.send_script) if args.send_script else default_send_script_path()
            send_host = str(resolve_send_option(args.send_host, zed_settings, "send_host", "127.0.0.1"))
            send_port = int(resolve_send_option(args.send_port, zed_settings, "send_port", 9009))
            send_timeout = float(resolve_send_option(args.send_timeout, zed_settings, "send_timeout", 5.0))
            result = run_send_runtime_config(
                config_path=output_path,
                send_script=send_script,
                host=send_host,
                port=send_port,
                timeout=send_timeout,
            )
            if result.stdout:
                print(result.stdout.rstrip())
            if result.stderr:
                print(result.stderr.rstrip(), file=sys.stderr)
            if result.returncode != 0:
                return result.returncode
            print(
                f"Sent config to JVM via {send_script} "
                f"({send_host}:{send_port})"
            )
        return 0

    if args.send:
        raise ValueError("--send requires --write-default or --output-file so the payload can be materialized")

    json.dump(config, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
