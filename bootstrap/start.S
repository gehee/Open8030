/* SPDX-License-Identifier: MIT */
.section .text.entry,"ax"
.global _start
.type _start,@function
_start:
    .option push
    .option norelax
    li gp, 0x200000
    li sp, 0x201000
    .option pop
    csrw mscratch, sp
    mv t0, gp
1:
    sw zero, 0(t0)
    addi t0, t0, 4
    bltu t0, sp, 1b
    call ar_bootstrap_main
2:
    j 2b
.size _start, .-_start
