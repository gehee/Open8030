# Third-party dependencies

The [MIT License](LICENSE) covers original Open8030 code and documentation.
External components retain their own licenses and attribution requirements.

- **Zephyr:** fetched separately at the revision in `west.yml` and
  `toolchain.json`. Zephyr is primarily Apache-2.0, with component-specific
  exceptions. Consult that pinned checkout's `LICENSE`, licensing documentation
  and individual source notices when distributing a firmware image.
- **Zephyr SDK / GNU tools / QEMU:** downloaded separately into `.local/tools/`.
  Their respective licenses and runtime-library terms are unchanged. Compiler
  and library obligations depend on the components included in a distributed image.
- **Python build packages:** pinned in `requirements-build.txt` and
  installed separately. Each package retains its own license.
- **zlib:** used only by native host tests as an independent CRC oracle; it is
  not linked into the radio firmware by those tests.

The board profiles in `profiles/` hold hardware data (register defaults, pin data,
frame-header keys) reverse engineered from the stock radio firmware. The receiver
loader JSON is not distributed in this repository. Container headers and empty
configuration space are generated from source; packaging does not require a stock
firmware image.

Binary releases include `LICENSES.txt` with the Open8030 license, Zephyr
license and source notices (including its MIT/BSD libc components), and the GCC
runtime license/exception. Keep that file with redistributed release firmware.

This file describes dependency boundaries; it is not a complete license inventory
for every possible Zephyr build configuration. Preserve the applicable upstream
notices when packaging dependencies or binaries.
