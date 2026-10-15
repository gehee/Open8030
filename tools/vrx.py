#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host transport and session controls for the tested Ascent VRX."""
import json
import hashlib
import re
import shlex
import subprocess
import tempfile
import uuid
from pathlib import Path
import socket
import struct
import time
def encode(reqid, msgid, payload=b""):
    header = b"\xaa" + struct.pack("<I", len(payload)) + struct.pack(">IIi", reqid, msgid, 0)
    checksum = 0xff
    for byte in header:
        checksum ^= byte
    return header + bytes([checksum]) + payload + b"\xbb"



def exact(connection, length):
    result = bytearray()
    while len(result) < length:
        chunk = connection.recv(length - len(result))
        if not chunk:
            raise EOFError("radio daemon disconnected")
        result.extend(chunk)
    return bytes(result)


def request(connection, opcode, work=0, payload=b""):
    started = time.monotonic()
    connection.sendall(encode(opcode, work, payload))
    header = exact(connection, 18)
    size = struct.unpack_from("<I", header, 1)[0]
    check = 0xff
    for byte in header[:17]:
        check ^= byte
    if header[0] != 0xaa or header[17] != check or size > 65536:
        raise ValueError("invalid daemon frame header/extent")
    response_opcode, message_id, status = struct.unpack_from(">IIi", header, 5)
    data = exact(connection, size)
    if exact(connection, 1) != b"\xbb" or response_opcode != opcode:
        raise ValueError("unexpected daemon response")
    # The daemon translates SDK work IDs to chip message IDs; they need not echo.
    return {"opcode": hex(opcode), "response_message_id": message_id, "status": status,
            "bytes": len(data), "payload_hex": data.hex(),
            "rpc_round_trip_ms": (time.monotonic() - started) * 1000}, data


def check(host, port=50000):
    with socket.create_connection((host, port), timeout=3) as connection:
        response, _ = request(connection, 0x0a000005)
        if response['status'] != 0:
            raise ValueError('Daemon initialization failed')
        response, devices = request(connection, 0x0a000000)
        if response['status'] < 0 or len(devices) < 4 or len(devices) % 4:
            raise ValueError('No selectable radio work ID')
        work = struct.unpack_from('<I', devices)[0]
        response, _ = request(connection, 0x0a000001, work)
        if response['status'] < 0:
            raise ValueError('Radio selection failed')
        response, identity = request(connection, 0x015a0000, work)
        if response['status'] != 0 or not identity.startswith(b'ar8030-zephyr/'):
            raise ValueError('Replacement firmware identity not confirmed')
        response, status = request(connection, 0x01000000, work, b'\xff\x3f')
        if response['status'] != 0 or len(status) != 300 or status[252] != 2:
            raise ValueError('Replacement firmware is not RF-linked')
        return {'identity': identity.rstrip(b'\0').decode('ascii'),
                'link_state': status[252], 'video_verified': False}



ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / '.local'


def validate_host(host):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]*', host):
        raise ValueError('Use a VRX IPv4 address or hostname, without a username or shell characters')
    return host


class Remote:
    """Reuse one normal SSH login for the operation; retain host-key checking."""
    def __init__(self, host):
        self.host = validate_host(host)
        self.directory = None

    def __enter__(self):
        self.directory = tempfile.TemporaryDirectory(prefix='open8030-ssh-')
        self.options = ['-o', 'ControlMaster=auto', '-o', 'ControlPersist=30',
                        '-o', f'ControlPath={self.directory.name}/socket',
                        '-o', 'HostKeyAlgorithms=+ssh-rsa', '-o', 'ConnectTimeout=10', '-o', 'ServerAliveInterval=10',
                        '-o', 'ServerAliveCountMax=3']
        return self

    def __exit__(self, *args):
        try:
            subprocess.run(['ssh', *self.options, '-O', 'exit', f'root@{self.host}'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        finally:
            self.directory.cleanup()

    def ssh(self, command, check=True):
        return subprocess.run(['ssh', *self.options, f'root@{self.host}', command],
                              check=check, text=True, stdout=subprocess.PIPE)

    def upload(self, path, destination):
        subprocess.run(['scp', '-O', *self.options, str(path), f'root@{self.host}:{destination}'], check=True)


def state_path(host):
    validate_host(host)
    key = hashlib.sha256(host.encode()).hexdigest()[:16]
    return LOCAL / f'vrx-{key}.json'


def load_stage(host):
    record = json.loads(state_path(host).read_text())
    stage = record['stage']
    if record['host'] != host or not re.fullmatch(r'/tmp/ar8030-zephyr-[A-Za-z0-9_-]+', stage):
        raise ValueError('Invalid saved VRX session')
    return stage


def run_vrx(host):
    """Start a bounded session, then retain it after native identity/link checks."""
    image = LOCAL / 'open8030.img'
    expected = json.loads((ROOT / 'baseline.json').read_text())['generated_container']['sha256']
    if not image.is_file() or hashlib.sha256(image.read_bytes()).hexdigest() != expected:
        raise ValueError('Build the accepted image first with ./open8030 build')
    record = state_path(host)
    stage = '/tmp/ar8030-zephyr-' + uuid.uuid4().hex[:16]
    with Remote(host) as remote:
        if record.exists():
            old = load_stage(host)
            active = remote.ssh(f'test -d {old} && test ! -e {old}/restored && '
                                'true', check=False)
            if active.returncode == 0:
                raise ValueError('A session is already active; use ./open8030 status or stop for this host')
            if active.returncode != 1:
                raise RuntimeError('Could not determine the previous VRX session state')
        remote.ssh(f'mkdir {stage} && cp /usrdata/fpv/boot_ar8030/bb_config_gnd.json {stage}/')
        for source, name in ((image, 'candidate.img'), (ROOT / 'tools/vrx.sh', 'vrx.sh')):
            remote.upload(source, f'{stage}/{name}')
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            remote.ssh(f'test "$(sha256sum {stage}/{name} | cut -d \' \' -f 1)" = {digest}')
        record.write_text(json.dumps({'host': host, 'stage': stage}, indent=2) + '\n')
        launch = 'sh "$1/vrx.sh" start "$1" 300; result=$?; printf "%s\\n" "$result" > "$1/exit.status"'
        remote.ssh(f'nohup sh -c {shlex.quote(launch)} sh {stage} > {stage}/run.log 2>&1 < /dev/null &')
        # Wait for completion of service startup before polling the radio daemon.
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            probe = remote.ssh(f'grep -q STACK_NATIVE_WINDOW_BEGIN {stage}/run.log', check=False)
            if probe.returncode == 0:
                break
            if probe.returncode not in (1,):
                raise RuntimeError(f'Could not read startup state; inspect {stage}/run.log')
            exited = remote.ssh(f'test -f {stage}/exit.status', check=False)
            if exited.returncode == 0:
                raise RuntimeError(f'Startup exited; inspect {stage}/run.log and exit.status')
            time.sleep(1)
        else:
            raise RuntimeError(f'Startup did not complete; inspect {stage}/run.log. The rollback timer remains active if armed.')
        deadline = time.monotonic() + 30
        while True:
            try:
                result = check(host)
                break
            except (OSError, ValueError, EOFError):
                if time.monotonic() >= deadline:
                    raise RuntimeError('RF link was not confirmed. Automatic rollback remains armed.')
                time.sleep(1)
        retained = remote.ssh(shlex.join(['sh', f'{stage}/vrx.sh', 'retain', stage]))
        print(retained.stdout, end='')
    print(json.dumps(result, indent=2))
    print(f'Open8030 stays running until stop or reboot. Logs: {stage}')
    print('Check fresh camera video separately; identity/link checks do not verify the picture.')



def stop_vrx(host):
    stage = load_stage(host)
    with Remote(host) as remote:
        # Stop is for a session which has finished starting, or finished rollback.
        # Avoid racing service startup by requiring its observation-window marker.
        remote.ssh(f'grep -q STACK_NATIVE_WINDOW_BEGIN {stage}/run.log || test -f {stage}/restored')
        result = remote.ssh(shlex.join(['sh', f'{stage}/vrx.sh', 'stop', stage]))
        print(result.stdout, end='')
        print(remote.ssh(f'tail -n 5 {stage}/restore.log').stdout, end='')
    print('Stock restoration completed. Verify fresh stock video.')
