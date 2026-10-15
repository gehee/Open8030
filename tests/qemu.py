#!/usr/bin/env python3
"""Exercise the actual Zephyr ELF over a private QEMU UART; never target hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import select
import struct
import subprocess
import time
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from vrx import encode


def receive(stream, pending, deadline):
    while time.monotonic() < deadline:
        if len(pending) >= 19:
            if pending[0] != 0xaa:
                raise ValueError(f"Unexpected console bytes: {pending[:32].hex()}")
            length = struct.unpack_from("<I", pending, 1)[0]
            if length > 4096:
                raise ValueError("unbounded reply")
            if len(pending) >= 19 + length:
                frame = bytes(pending[:19 + length])
                del pending[:19 + length]
                check = 0xff
                for byte in frame[:17]:
                    check ^= byte
                if check != frame[17] or frame[-1] != 0xbb:
                    raise ValueError("corrupt reply")
                reqid, msgid, status = struct.unpack_from(">IIi", frame, 5)
                return reqid, msgid, status, frame[18:-1]
        ready, _, _ = select.select([stream], [], [], max(0, deadline - time.monotonic()))
        if ready:
            chunk = stream.read1(8192)
            if not chunk:
                raise RuntimeError("QEMU output ended")
            pending.extend(chunk)
    raise TimeoutError("Zephyr UART response timeout")


def test(qemu, elf):
    command = [str(qemu), "-machine", "virt", "-cpu", "rv32", "-m", "256M",
               "-bios", "none", "-kernel", str(elf), "-display", "none",
               "-monitor", "none", "-serial", "stdio"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    pending, results = bytearray(), []
    try:
        cases = [(0xff000000, b"", b"\x00\x10\x00\x00ZEPH", 0),
                 (0xff000002, bytes(range(256)) * 16, bytes(range(256)) * 16, 0),
                 (0x0c5a0000, b"", b"ar8030-zephyr/bringup;rf=unsupported;protocol=1", 0),
                 (0x0b000000, b"", b"", -95)]
        for msgid, (reqid, payload, expected, expected_status) in enumerate(cases, 1):
            frame = encode(reqid, msgid, payload)
            if msgid == 1:
                damaged = bytearray(frame)
                damaged[17] ^= 1
                process.stdin.write(b"noise" + damaged)
            # Split a header and payload across writes to exercise stream parsing.
            process.stdin.write(frame[:7])
            process.stdin.flush()
            process.stdin.write(frame[7:])
            process.stdin.flush()
            reply_req, reply_msg, status, data = receive(process.stdout, pending, time.monotonic() + 10)
            if (reply_req, reply_msg, status, data) != (reqid, msgid, expected_status, expected):
                raise AssertionError(f"Unexpected response for {reqid:#x}: {(reply_req, reply_msg, status, len(data))}")
            results.append({"request": hex(reqid), "msgid": msgid, "status": status,
                            "payload_bytes": len(data), "passed": True})
        return {"platform": "qemu-riscv32", "hardware_radio_tested": False,
                "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(), "cases": results}
    finally:
        process.terminate()
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = test(args.qemu.resolve(), args.elf.resolve())
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        with args.output.open("x") as output:
            output.write(text)
    print(text, end="")
