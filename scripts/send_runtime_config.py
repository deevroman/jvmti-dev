#!/usr/bin/env python3

import argparse
import json
import socket
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send runtime config payload to dumper control socket."
    )
    parser.add_argument("--host", default="127.0.0.1", help="Socket host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9009, help="Socket port (default: 9009)")
    parser.add_argument(
        "--timeout", type=float, default=5.0, help="Connection timeout in seconds (default: 5)"
    )

    parser.add_argument("--payload", help="Raw payload string (JSON or key:value pairs)")
    parser.add_argument("--payload-file", help="File with raw payload to send")

    parser.add_argument("--target-class", help="Target class name")
    parser.add_argument("--target-method", help="Target method name")
    parser.add_argument("--target-signature", help="Target method descriptor, e.g. (II)I")
    parser.add_argument("--dump", help="Optional dump output path")
    parser.add_argument("--llm-dump", help="Optional LLM-readable dump output path")
    return parser.parse_args()


def build_payload(args: argparse.Namespace) -> str:
    if args.payload and args.payload_file:
        raise ValueError("Use only one of --payload or --payload-file")

    if args.payload_file:
        raw = Path(args.payload_file).read_text(encoding="utf-8")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            return raw
        if isinstance(parsed, dict):
            return json.dumps(parsed, ensure_ascii=True, separators=(",", ":"))
        return raw

    if args.payload:
        return args.payload

    required = [args.target_class, args.target_method, args.target_signature]
    if any(v is None for v in required):
        raise ValueError(
            "Either provide --payload/--payload-file or all of "
            "--target-class, --target-method, --target-signature"
        )

    payload = {
        "target_class": args.target_class,
        "target_method": args.target_method,
        "target_method_signature": args.target_signature,
    }
    if args.dump:
        payload["dump"] = args.dump
    if args.llm_dump:
        payload["llm_dump"] = args.llm_dump
    return json.dumps(payload, ensure_ascii=True)


def main() -> int:
    args = parse_args()

    try:
        payload = build_payload(args)
    except Exception as exc:  # noqa: BLE001
        print(f"Invalid payload input: {exc}", file=sys.stderr)
        return 2

    if not payload.endswith("\n"):
        payload += "\n"

    try:
        with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
            sock.sendall(payload.encode("utf-8"))
            response = sock.recv(4096).decode("utf-8", errors="replace").strip()
    except OSError as exc:
        print(f"Socket error: {exc}", file=sys.stderr)
        return 1

    print(response)
    return 0 if not response.startswith("ERROR") else 3


if __name__ == "__main__":
    raise SystemExit(main())
