# SPDX-License-Identifier: MIT
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import telemetry


class TelemetryTests(unittest.TestCase):
    def snapshot(self, now=100, updated=0, linked=1, seen=31):
        data = bytearray(444)
        struct.pack_into('<4I', data, 0, 1, linked, now, seen)
        bodies = [b'\x05\xfa' + bytes(9) + struct.pack('<I', 12345), b'TEST', b'\x04\x05\x06',
                  struct.pack('<HHHIB', 125, 9, 3, 0x1234, 2),
                  struct.pack('<BHHhH', 120, 456, 512, -123, 1234)]
        for i, body in enumerate(bodies):
            offset = 64 + i * 76
            struct.pack_into('<3I', data, offset, updated, 1, len(body))
            data[offset+12:offset+12+len(body)] = body
        return data

    def test_units_and_signs(self):
        r = telemetry.decode(self.snapshot())['records']
        self.assertEqual(r['transmitter']['values'], {'voltage_v': 12.345, 'temperature_c': -6})
        self.assertEqual(r['fc_analog']['values'], {'voltage_v': 12.34, 'consumed_mah': 456,
                                                  'rssi_raw': 512, 'current_a': -1.23})
        self.assertEqual(r['fc_status']['values']['mode_flags'], 0x1234)
        self.assertNotIn('armed', r['fc_status']['values'])
        self.assertEqual(r['fc_version']['values']['version'], '4.5.6')

    def test_variant_is_safe_for_terminal_output(self):
        data = self.snapshot()
        data[152:156] = b'\x1b[2J'
        result = telemetry.decode(data)
        self.assertEqual(result['records']['fc_variant']['values']['identifier'], '?[2J')
        self.assertNotIn('\x1b', telemetry.format_text(result))

    def test_legacy_analog(self):
        data = self.snapshot()
        struct.pack_into('<I', data, 64+4*76+8, 7)
        self.assertEqual(telemetry.decode(data)['records']['fc_analog']['values']['voltage_v'], 12.0)

    def test_freshness_missing_link_and_wrap(self):
        for now, updated, state, age in [(5000, 0, 'current', 5000), (5001, 0, 'stale', 5001),
                                         (50, 0xfffffff0, 'current', 66)]:
            record = telemetry.decode(self.snapshot(now, updated))['records']['transmitter']
            self.assertEqual((record['state'], record['age_ms']), (state, age))
        r = telemetry.decode(self.snapshot(seen=0))['records']['transmitter']
        self.assertEqual((r['state'], r['age_ms'], r['values']), ('missing', None, None))
        self.assertEqual(telemetry.decode(self.snapshot(linked=0))['records']['transmitter']['state'], 'stale')

    def test_invalid_snapshot(self):
        for offset, value in [(0, 2), (4, 2), (12, 32), (72, 65), (72, 14)]:
            data = self.snapshot(); struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError):
                telemetry.decode(data)
        with self.assertRaises(ValueError):
            telemetry.decode(b'')

    def test_output_marks_stale_and_missing(self):
        result = telemetry.decode(self.snapshot(now=6000, seen=1))
        text = telemetry.format_text(result)
        self.assertIn('STALE, age 6000 ms', text)
        self.assertIn('no valid message received', text)
