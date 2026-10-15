# SPDX-License-Identifier: MIT
"""Read-only transmitter and MSP telemetry snapshots from Open8030."""
import json
import socket
import struct
import time

import vrx

NAMES = ('transmitter', 'fc_variant', 'fc_version', 'fc_status', 'fc_analog')
MINIMUM = (15, 4, 3, 11, 7)
COUNTERS = ('sessions', 'bytes', 'frames', 'bad_header', 'bad_crc', 'ignored',
            'msp_frames', 'msp_bad', 'short_records', 'partial_resets', 'msp_oversize')


def values(index, data):
    u16 = lambda offset: struct.unpack_from('<H', data, offset)[0]
    u32 = lambda offset: struct.unpack_from('<I', data, offset)[0]
    if index == 0:
        return {'voltage_v': u32(11) / 1000,
                'temperature_c': struct.unpack_from('b', data, 1)[0]}
    if index == 1:
        return {'identifier': ''.join(chr(v) if 32 <= v < 127 else '?' for v in data[:4])}
    if index == 2:
        return {'version': '.'.join(str(v) for v in data[:3])}
    if index == 3:
        # Mode bit meanings require the FC's box mapping; don't infer armed state.
        return {'cycle_time_us': u16(0), 'i2c_errors': u16(2),
                'sensor_mask': u16(4), 'mode_flags': u32(6), 'pid_profile': data[10]}
    return {'voltage_v': u16(7) / 100 if len(data) >= 9 else data[0] / 10,
            'consumed_mah': u16(1), 'rssi_raw': u16(3),
            'current_a': struct.unpack_from('<h', data, 5)[0] / 100}


def decode(data, stale_ms=5000):
    if len(data) != 444:
        raise ValueError('Unsupported telemetry snapshot length; build and run the latest firmware')
    header = struct.unpack_from('<16I', data)
    version, linked, now, seen = header[:4]
    if version != 1 or linked not in (0, 1) or seen & ~31:
        raise ValueError('Unsupported telemetry snapshot format')
    result = {'linked': bool(linked), 'freshness_threshold_ms': stale_ms,
              'counters': dict(zip(COUNTERS, header[4:15])), 'records': {}}
    for i, name in enumerate(NAMES):
        offset = 64 + 76 * i
        updated, count, length = struct.unpack_from('<3I', data, offset)
        received = bool(seen & (1 << i))
        if length > 64 or (received and length < MINIMUM[i]):
            raise ValueError(f'Invalid {name} record length')
        age = ((now - updated) & 0xffffffff) if received else None
        state = ('missing' if not received else
                 'current' if linked and age <= stale_ms else 'stale')
        result['records'][name] = {'state': state, 'age_ms': age, 'messages': count,
                                  'values': values(i, data[offset+12:offset+12+length]) if received else None}
    return result


def select(connection):
    response, _ = vrx.request(connection, 0x0a000005)
    if response['status'] != 0:
        raise ValueError('Daemon initialization failed')
    response, devices = vrx.request(connection, 0x0a000000)
    if response['status'] < 0 or len(devices) < 4 or len(devices) % 4:
        raise ValueError('No selectable radio work ID')
    work = struct.unpack_from('<I', devices)[0]
    response, _ = vrx.request(connection, 0x0a000001, work)
    if response['status'] < 0:
        raise ValueError('Radio selection failed')
    response, identity = vrx.request(connection, 0x015a0000, work)
    if response['status'] != 0 or not identity.startswith(b'ar8030-zephyr/'):
        raise ValueError('Open8030 firmware identity not confirmed')
    return work


def snapshot(connection, work):
    response, data = vrx.request(connection, 0x015a002c, work)
    if response['status'] != 0:
        raise ValueError('Telemetry diagnostics unavailable; build and run the latest firmware')
    return decode(data)


def format_text(result):
    lines = [f"Radio link: {'connected' if result['linked'] else 'disconnected'}"]
    for name, record in result['records'].items():
        label = name.replace('_', ' ')
        if record['state'] == 'missing':
            lines.append(f'{label}: no valid message received in this link session')
        else:
            fields = ', '.join(f'{key}={value}' for key, value in record['values'].items())
            lines.append(f"{label}: {record['state'].upper()}, age {record['age_ms']} ms; {fields}")
    return '\n'.join(lines)


def run(host, watch=False, as_json=False):
    with socket.create_connection((host, 50000), timeout=3) as connection:
        work = select(connection)
        while True:
            result = snapshot(connection, work)
            print(json.dumps(result, indent=None if watch else 2) if as_json else format_text(result), flush=True)
            if not watch:
                return
            if not as_json:
                print(flush=True)
            time.sleep(1)
