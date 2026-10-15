# SPDX-License-Identifier: MIT
set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld CACHE INTERNAL "")
zephyr_include_directories(.)
zephyr_sources(reset_hook.S)
