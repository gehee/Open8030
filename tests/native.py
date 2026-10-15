#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile and run synthetic native firmware tests; never access a device."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TESTS = {
    'frame': ('frame dispatch', [], []),
    'command': ('command', [], []),
    'scan': ('scan', [], []),
    'scan_scores': ('scan_scores', [], []),
    'catalog': ('catalog scan', [], []),
    'channel_work': ('channel_work', [], []),
    'channel_policy': ('channel_policy channel_work', [], []),
    'channel_transaction': ('channel_transaction channel_work', [], []),
    'channel_link': ('channel_link', [], []),
    'channel_monitor': ('channel_monitor', [], []),
    'phy_control': ('phy_control mac', [], []),
    'phy_receive': ('phy_receive phy_control mac ap_time', [], []),
    'interleaver': ('interleaver', [], []),
    'tx_mcs': ('tx_mcs interleaver', [], []),
    'tx_irq': ('tx_irq', [], []),
    'tx_retire': ('tx_retire', [], []),
    'tx_stream': ('tx_stream tx_retire', [], []),
    'tx_acquire': ('tx_stream', [], []),
    'tx_write': ('tx_stream', [], []),
    'tx_close': ('tx_stream tx_retire', [], []),
    'tx_open': ('tx_stream', [], []),
    'tx_alloc': ('tx_alloc tx_stream', [], []),
    'tx_handle': ('tx_handle tx_stream tx_retire', [], []),
    'tx_handle_open': ('tx_handle tx_stream', [], []),
    'tx_packet': ('tx_packet tx_stream tx_retire mac', [], []),
    'tune': ('tune', [], []),
    'socket_rx': ('socket_rx', [], []),
    'socket_shutdown': ('socket_shutdown', [], []),
    'sdio_device': ('sdio_device', ['AR_SDIO_SIMULATED', 'AR_SDIO_STOCK_MEMORY_ATTRIBUTES=1'], []),
    'mac': ('mac', [], []),
    'dev': ('dev mac', [], []),
    'dev_tx': ('dev_tx mac dev', [], []),
    'dev_mcs': ('dev_mcs', [], []),
    'ap_time': ('ap_time', [], []),
    'fch_monitor': ('fch_monitor', [], []),
    'event': ('event', [], []),
    'data_event': ('data_event', [], []),
    'data_framing': ('data_framing data_event', [], []),
    'data_route': ('data_route data_event', [], []),
    'data_writer': ('data_route data_event', [], []),
    'data_ingress': ('data_ingress', [], []),
    'data_socket': ('data_socket data_route', [], []),
    'data_pipeline': ('data_framing data_filter data_dispatch data_worker data_action data_builder data_route data_event', [], []),
    'data_filter': ('data_filter', [], []),
    'data_dispatch': ('data_dispatch data_filter', [], []),
    'data_action': ('data_action data_route', [], []),
    'data_builder': ('data_builder', [], []),
    'data_worker': ('data_worker data_action data_dispatch data_filter data_route', [], []),
    'json': ('json', [], []),
    'config': ('config json power_config power dev_mcs', [], []),
    'power': ('power', [], []),
    'power_control': ('power_control power', [], []),
    'power_host': ('power_host power_control power', [], []),
    'power_config': ('power_config power json', [], []),
    'flash': ('flash', [], ['-lz']),
    'flash_spi': ('flash_spi sfdp', [], []),
    'sfdp': ('sfdp', [], []),
    'factory_storage': ('flash flash_spi sfdp factory power_config power json', [], ['-lz']),
    'boot': ('boot config json power_config power dev_mcs factory flash_spi sfdp flash', [], []),
    'rf_device': ('rf_device mac dev dev_tx dev_mcs tx_mcs interleaver tx_irq fch_monitor channel_monitor power power_control ap_time command scan scan_scores tune phy_control phy_receive', ['AR_RF_SIMULATED'], []),
    'bootstrap': ('', ['AR_BOOTSTRAP_SIMULATED'], []),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc', help='Native Linux C compiler')
    parser.add_argument('--test', choices=sorted(TESTS), action='append', help='Run selected tests; default is all')
    args = parser.parse_args()
    selected = args.test or list(TESTS)
    output = ROOT / '.local/native-tests'
    output.mkdir(parents=True, exist_ok=True)
    for name in selected:
        modules, defines, libraries = TESTS[name]
        sources = [ROOT / f'src/{module}.c' for module in modules.split()]
        if name == 'bootstrap':
            sources.append(ROOT / 'bootstrap/bootstrap.c')
        binary = output / name
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                   '-I', str(ROOT / 'include'), '-I', str(ROOT / 'bootstrap'),
                   *['-D' + define for define in defines], *map(str, sources),
                   str(ROOT / f'tests/{name}_test.c'), *libraries, '-o', str(binary)]
        print(f'Building and running {name}', flush=True)
        subprocess.run(command, check=True, timeout=120)
        subprocess.run([str(binary)], check=True, timeout=120)
    print(f'Passed {len(selected)} native firmware test suites.')


if __name__ == '__main__':
    main()
