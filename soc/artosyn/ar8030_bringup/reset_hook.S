/* SPDX-License-Identifier: MIT */
/* The stock startup identifies T-Head MHCR/cache operations. For diagnostic
 * DMA bring-up keep both caches disabled; no unported cache-coherency claim. */
.section .text.soc_reset_hook,"ax"
.global soc_reset_hook
.type soc_reset_hook,@function
soc_reset_hook:
    csrci mstatus, 8
    csrw mie, zero
    li t0, 0xe0800008
    li t1, 0xff000000
    sw t1, 0(t0) /* CLIC delivery threshold: pending bits remain pollable. */
    .4byte 0x0020000b /* th.dcache.iall */
    .4byte 0x0100000b /* th.icache.iall */
    csrr t0, 0x7c1
    andi t0, t0, -4
    csrw 0x7c1, t0
    fence rw,rw
    fence.i
    ret
