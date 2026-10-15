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

Published firmware images include compiled matched board-profile data. The profile
source and receiver loader JSON are not distributed in this repository, and
Open8030's MIT license does not relicense those private inputs. Container headers
and empty configuration space are generated from source; packaging does not
require a stock firmware image. A source checkout alone does not supply the
private inputs needed to reproduce the RF video image.

The v0.1.0 binary release includes `LICENSES.txt` with the Open8030 license, Zephyr
license and source notices (including its MIT/BSD libc components), and the GCC
runtime license/exception. Keep that file with redistributed release firmware.

This file describes dependency boundaries; it is not a complete license inventory
for every possible Zephyr build configuration. Preserve the applicable upstream
notices when packaging dependencies or binaries.
