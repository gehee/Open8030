/* SPDX-License-Identifier: MIT */
#ifndef AR_BOOTSTRAP_H
#define AR_BOOTSTRAP_H
#include <stdint.h>
enum ar_bootstrap_result { AR_BOOTSTRAP_RESET, AR_BOOTSTRAP_ROM_FALLBACK };
/* Matched ROM scratch contract; not a portable oscillator/calibration API. */
int8_t ar_bootstrap_trim(int8_t previous, uint32_t compare, uint32_t counter, uint32_t reference);
enum ar_bootstrap_result ar_bootstrap_run(void);
#endif
