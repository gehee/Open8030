/* SPDX-License-Identifier: MIT */
#ifndef AR_CACHE_H
#define AR_CACHE_H
#include <stddef.h>
#include <stdint.h>
/* T-Head data-cache maintenance by physical address, 32-byte lines, as the
 * stock firmware's 0x217948 (invalidate), 0x21796e (clean) and 0x217996
 * (clean+invalidate) helpers do, fences before and after. The attribute
 * table at 0xeffff000 makes RAM 0x200000..0x300000 the only cached region,
 * so only buffers the baseband, USB or SDIO engines read or write need this:
 * clean before an engine reads a CPU-written buffer, invalidate before the
 * CPU reads an engine-written one (and flush before arming such a buffer so
 * no dirty line lands on the engine's data). */
#define AR_CACHE_LINE 32u
#if defined(__riscv)
#define AR_CACHE_OP(insn, address) do { \
	register uintptr_t ar_cache_a5 __asm__("a5") = (address); \
	__asm__ volatile(".word " insn :: "r"(ar_cache_a5) : "memory"); } while (0)
static inline void ar_cache_range(const void *data, size_t length, unsigned op)
{
	uintptr_t address = (uintptr_t)data & ~(uintptr_t)(AR_CACHE_LINE - 1);
	uintptr_t end = ((uintptr_t)data + length + AR_CACHE_LINE - 1) & ~(uintptr_t)(AR_CACHE_LINE - 1);
	__asm__ volatile("fence" ::: "memory");
	for (; address < end; address += AR_CACHE_LINE) {
		if (op == 0) { AR_CACHE_OP("0x02a7800b", address); }      /* th.dcache.ipa a5 */
		else if (op == 1) { AR_CACHE_OP("0x0297800b", address); } /* th.dcache.cpa a5 */
		else { AR_CACHE_OP("0x02b7800b", address); }              /* th.dcache.cipa a5 */
	}
	__asm__ volatile("fence\n\tfence" ::: "memory");
}
#else
/* Host builds (native tests): no data cache to maintain. */
static inline void ar_cache_range(const void *data, size_t length, unsigned op) { (void)data; (void)length; (void)op; }
#endif
static inline void ar_cache_invalidate(const void *data, size_t length) { ar_cache_range(data, length, 0); }
static inline void ar_cache_clean(const void *data, size_t length) { ar_cache_range(data, length, 1); }
static inline void ar_cache_flush(const void *data, size_t length) { ar_cache_range(data, length, 2); }
#endif
