# SPDX-License-Identifier: MIT
"""Offline checks for packaging, VRX identity and loader preflight rejection."""
from pathlib import Path
import struct
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build as pack
import vrx as vrx_status
from vrx import encode


class Connection:
    def __init__(self, replies):
        self.data = bytearray(b''.join(replies))
        self.sent = []

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, length):
        # Exercise partial TCP reads as well as frame boundaries.
        length = min(length, 7)
        data = bytes(self.data[:length])
        del self.data[:length]
        return data


def replies(identity=b'ar8030-zephyr/bringup', linked=True):
    status = bytearray(300)
    status[252] = 2 if linked else 0
    return [encode(opcode, 0, data) for opcode, data in (
        (0x0a000005, b''), (0x0a000000, struct.pack('<I', 1)),
        (0x0a000001, b''), (0x015a0000, identity), (0x01000000, status))]


class StatusTests(unittest.TestCase):
    def test_native_link_with_fragmented_replies(self):
        connection = Connection(replies())
        with patch.object(vrx_status.socket, 'create_connection', return_value=connection):
            result = vrx_status.check('test.invalid')
        self.assertEqual(result['link_state'], 2)
        self.assertFalse(result['video_verified'])
        self.assertEqual(len(connection.sent), 5)

    def test_positive_device_count_is_success(self):
        frames = replies()
        devices = bytearray(frames[1])
        struct.pack_into('>i', devices, 13, 1)
        check = 0xff
        for byte in devices[:17]:
            check ^= byte
        devices[17] = check
        frames[1] = bytes(devices)
        with patch.object(vrx_status.socket, 'create_connection', return_value=Connection(frames)):
            self.assertEqual(vrx_status.check('test.invalid')['link_state'], 2)

    def test_stock_identity_rejected(self):
        with patch.object(vrx_status.socket, 'create_connection', return_value=Connection(replies(b'stock'))):
            with self.assertRaisesRegex(ValueError, 'identity'):
                vrx_status.check('test.invalid')

    def test_unlinked_native_rejected(self):
        with patch.object(vrx_status.socket, 'create_connection', return_value=Connection(replies(linked=False))):
            with self.assertRaisesRegex(ValueError, 'RF-linked'):
                vrx_status.check('test.invalid')

    def test_corrupt_frame_rejected(self):
        frame = bytearray(encode(1, 0))
        frame[17] ^= 1
        with self.assertRaisesRegex(ValueError, 'header'):
            vrx_status.request(Connection([frame]), 1)

    def test_disconnected_daemon_rejected(self):
        with self.assertRaises(EOFError):
            vrx_status.request(Connection([]), 1)


class PackageTests(unittest.TestCase):
    def test_existing_output_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'candidate.img'
            out.write_bytes(b'existing')
            with self.assertRaises(FileExistsError):
                pack.package(Path('absent'), Path('absent'), out)
            self.assertEqual(out.read_bytes(), b'existing')

    def test_unmatched_runtime_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'runtime.bin'
            binary.write_bytes(b'wrong runtime')
            boot = Path(directory) / 'bootstrap.bin'
            boot.write_bytes(b'bootstrap')
            out = Path(directory) / 'candidate.img'
            with self.assertRaisesRegex(ValueError, 'baseline input'):
                pack.package(binary, boot, out)
            self.assertFalse(out.exists())

    def test_generated_layout_and_checksums(self):
        data, boot = bytes(range(251)) * 3, b'source bootstrap'
        image = pack.make_image(data, boot)
        header, segments, _ = pack.parse_image(image)
        self.assertEqual(header['spl_load_address'], 0x204000)
        self.assertEqual(header['signature_load_address'], 0x200000)
        self.assertEqual(header['troot_load_address'], 0x0a098a00)
        self.assertEqual(segments['troot']['data'], boot)
        self.assertEqual(segments['signature']['data'], bytes(16384))
        payload = segments['spl']['data']
        self.assertEqual(payload[:len(data)], data)
        self.assertEqual(payload[len(data):], bytes(1024 - len(data)))
        check_header = bytearray(image[:64])
        struct.pack_into('<I', check_header, 8, 0)
        self.assertEqual(header['header_checksum'], sum(struct.unpack('<16I', check_header)) & 0xffffffff)
        self.assertEqual(struct.unpack_from('<Q', image, 52)[0],
                         sum(struct.unpack('<128Q', payload)) & 0xffffffffffffffff)

    def test_reject_empty_or_oversized_components(self):
        for runtime, bootstrap in ((b'', b'boot'), (b'runtime', b''),
                                   (bytes(0xac001), b'boot'), (b'runtime', bytes(0x5f9))):
            with self.assertRaises(ValueError):
                pack.make_image(runtime, bootstrap)

    def test_truncated_header_rejected(self):
        with self.assertRaisesRegex(ValueError, 'truncated'):
            pack.parse_image(b'\0' * 20)


class LoaderPreflightTests(unittest.TestCase):
    def test_invalid_stage_rejected(self):
        for action in ('start', 'stop'):
            with self.subTest(action=action):
                result = subprocess.run(['sh', str(ROOT / 'tools/vrx.sh'), action, '/invalid-stage'],
                                        capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2)

    def test_missing_stage_files_rejected(self):
        # Both scripts must stop before accessing device state or running services.
        with tempfile.TemporaryDirectory(prefix='ar8030-zephyr-', dir='/tmp') as directory:
            for action in ('start', 'stop'):
                result = subprocess.run(['sh', str(ROOT / 'tools/vrx.sh'), action, directory],
                                        capture_output=True, timeout=5)
                self.assertNotEqual(result.returncode, 0)
            self.assertEqual(list(Path(directory).iterdir()), [])


class WorkflowTests(unittest.TestCase):
    def test_host_cannot_inject_commands_or_ssh_options(self):
        for host in ('-oProxyCommand=bad', 'host;id', 'root@host', 'host$(id)', 'host name'):
            with self.subTest(host=host), self.assertRaises(ValueError):
                vrx_status.validate_host(host)

    def test_saved_session_cannot_inject_a_remote_command(self):
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(vrx_status, 'LOCAL', Path(directory)):
            vrx_status.state_path('test.invalid').write_text(json.dumps({
                'host': 'test.invalid', 'stage': '/tmp/ar8030-zephyr-x;id'}))
            with self.assertRaisesRegex(ValueError, 'session'):
                vrx_status.load_stage('test.invalid')

    def fixture(self, directory):
        root = Path(directory)
        local = root / '.local'
        local.mkdir()
        (root / 'tools').mkdir()
        (root / 'tools/vrx.sh').write_text('synthetic control script')
        (local / 'open8030.img').write_bytes(b'synthetic accepted image')
        (root / 'baseline.json').write_text(json.dumps({'generated_container': {
            'sha256': hashlib.sha256(b'synthetic accepted image').hexdigest()}}))
        return root, local

    def test_run_stages_starts_and_retains_after_link_check(self):
        with tempfile.TemporaryDirectory() as directory:
            root, local = self.fixture(directory)
            remote = MagicMock()
            remote.ssh.return_value = subprocess.CompletedProcess([], 0, '')
            with patch.object(vrx_status, 'ROOT', root), patch.object(vrx_status, 'LOCAL', local), \
                 patch.object(vrx_status, 'Remote') as factory, \
                 patch.object(vrx_status, 'check', return_value={'link_state': 2}), \
                 patch('builtins.print'):
                factory.return_value.__enter__.return_value = remote
                vrx_status.run_vrx('test.invalid')
                stage = vrx_status.load_stage('test.invalid')
            self.assertEqual(remote.upload.call_count, 2)
            commands = [call.args[0] for call in remote.ssh.call_args_list]
            self.assertTrue(any('start "$1" 300' in command and stage in command for command in commands))
            self.assertTrue(any(f'vrx.sh retain {stage}' in command for command in commands))

    def test_run_unlinked_never_retains(self):
        with tempfile.TemporaryDirectory() as directory:
            root, local = self.fixture(directory)
            remote = MagicMock()
            remote.ssh.return_value = subprocess.CompletedProcess([], 0, '')
            with patch.object(vrx_status, 'ROOT', root), patch.object(vrx_status, 'LOCAL', local), \
                 patch.object(vrx_status, 'Remote') as factory, \
                 patch.object(vrx_status, 'check', side_effect=ValueError('unlinked')), \
                 patch.object(vrx_status.time, 'monotonic', side_effect=[0, 1, 0, 31]):
                factory.return_value.__enter__.return_value = remote
                with self.assertRaisesRegex(RuntimeError, 'rollback'):
                    vrx_status.run_vrx('test.invalid')
            self.assertFalse(any(' retain ' in call.args[0] for call in remote.ssh.call_args_list))

    def test_run_preserves_an_active_session(self):
        with tempfile.TemporaryDirectory() as directory:
            root, local = self.fixture(directory)
            remote = MagicMock()
            remote.ssh.return_value = subprocess.CompletedProcess([], 0, '')
            with patch.object(vrx_status, 'ROOT', root), patch.object(vrx_status, 'LOCAL', local), \
                 patch.object(vrx_status, 'Remote') as factory:
                record = vrx_status.state_path('test.invalid')
                saved = json.dumps({'host': 'test.invalid', 'stage': '/tmp/ar8030-zephyr-existing'})
                record.write_text(saved)
                factory.return_value.__enter__.return_value = remote
                with self.assertRaisesRegex(ValueError, 'already active'):
                    vrx_status.run_vrx('test.invalid')
                self.assertEqual(record.read_text(), saved)
                remote.upload.assert_not_called()


class MediaResetTests(unittest.TestCase):
    def reset_with(self, commands):
        script = (ROOT / 'tools/vrx.sh').read_text()
        body = script.split('reset_media() {', 1)[1].split('\n}\n', 1)[0]
        # Execute the real function with simulated loaded modules and commands;
        # never call the host's module loader during this test.
        return subprocess.run(['sh', '-c', 'test() { return 0; }\n' + commands +
                               '\nreset_media() {' + body + '\n}\nreset_media'],
                              text=True, capture_output=True, timeout=5)

    def test_busy_module_stops_before_allocator_reload(self):
        result = self.reset_with('''
rmmod() { printf 'remove %s\n' "$1"; [ "$1" != ar_vb ]; }
insmod() { printf 'load %s\n' "$1"; }
''')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('remove ar_osal', result.stdout)
        self.assertNotIn('load ', result.stdout)

    def test_allocator_load_failure_stops_dependent_loads(self):
        result = self.reset_with('''
rmmod() { return 0; }
insmod() { printf 'load %s\n' "$1"; return 1; }
''')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout.splitlines(), ['load /mod/ar_osal.ko'])


if __name__ == '__main__':
    unittest.main()
