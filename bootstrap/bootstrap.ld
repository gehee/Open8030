/* SPDX-License-Identifier: MIT */
ENTRY(_start)
MEMORY { BOOT (rx) : ORIGIN = 0x0a098a00, LENGTH = 0x5f8 }
SECTIONS {
    .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > BOOT
    .data : { *(.data*) *(.sdata*) } > BOOT
    .bss : { *(.bss*) *(.sbss*) *(COMMON) } > BOOT
    ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "Bootstrap has no initialized/global writable data contract")
    ASSERT(. <= 0x0a098ff8, "Bootstrap overlaps ROM timing scratch")
    /DISCARD/ : { *(.comment*) *(.eh_frame*) *(.note*) }
}
