/* BETAFPV P1 Nano VRX, SDIO receiver (DEV).
 * Host: AR9311 (artosyn_sdio.ko).
 * From the receiver's own stock firmware, bb_demo_gnd.img (c401 generation,
 * built Mar 10 2026 11:45:46), SHA-256
 * 372529a68b4d2847cec4f3de86316dd21cc641a2a8994226417d09860ab36e8c.
 * Host config: board "c401", tx_mode "1TX". Its board init (0x234dd6) maps
 * that to board type 4 ("c401_1t2r"): pin list at 0x204ea2, then the RF
 * front-end tail at 0x234fe6 (bank 3 pins 4-9 = functions 0x7f-0x84), mux
 * values through the function table at 0x245578.
 *
 * Verified offline against the image: board type, the 20 pin records, the
 * I/O domain calls (bank 2's bit set, bank 3's cleared -> 2), the FCH keys,
 * and that the default-register initializer (0x20c320) is the SteadyDigital
 * image's, instruction for instruction (same 704 initial writes), and does
 * not read the board type or tx_mode. No SPI flash pins are muxed (bank A
 * stays GPIO, as on the SteadyDigital): build with -DAR_FACTORY_OPTIONAL.
 *
 * The only pin difference from the SteadyDigital (type 5): C4 is a GPIO
 * (function 0x26, mux 0) instead of function 0x49 (mux 9). Stock also makes
 * C4 an OUTPUT and drives it LOW; a pin record cannot say that, so the host
 * must (PRJ_CMD_SET_GPIO word 0x4ca2: bank 2, pin 4, output, function 0x26,
 * level 0) - NOT done by this profile, untested.
 *
 * Assumed, not verified: the host driver replaces the image's built-in
 * config (c401 + 2TX_STBC, which alone would give type 5) with the host
 * file; C4's pad byte 0x20 is stock's pad argument without the low nibble it
 * takes from the live pad register (UNKNOWN offline; the P1 goggle profile
 * uses 0 there); bb_defaults.inc is shared as in the SteadyDigital profile
 * (both stock images' own initial writes differ from it in the same 29
 * records). Nothing here has run on the hardware.
 * Reverse engineered data, not vendor executable code.
 */
#include "ar_boot.h"

#include "bb_defaults.inc"

static const uint8_t pins[80] = {
    /* bank, pin, mux, pad - board type 4's pin list, then its RF front-end tail */
    0, 0, 0, 0,
    0, 1, 0, 0,
    0, 2, 0, 0,
    0, 3, 0, 0,
    0, 4, 0, 0,
    1, 0, 0, 0,
    1, 3, 1, 0,
    1, 4, 1, 0,
    2, 4, 0, 0x20,  /* GPIO; stock: output, low (see above) */
    2, 2, 6, 0,
    2, 5, 4, 0,
    3, 10, 0, 0,
    3, 11, 0, 0,
    3, 12, 0, 0,
    3, 4, 1, 0,
    3, 5, 1, 0,
    3, 6, 1, 0,
    3, 7, 1, 0,
    3, 8, 1, 0,
    3, 9, 1, 0,
};

/* FCH encoding keys: the MCS -> frame-control-header key table from this
 * receiver's stock firmware, runtime address 0x23a880; byte-identical to the
 * SteadyDigital and P1 goggle profiles' table. */
static const uint8_t fch_keys[100] = {
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00,
    0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00, 0x02, 0x01, 0x01, 0x00, 0x00, 0x02, 0x01,
    0x00, 0x01, 0x02, 0x01, 0x00, 0x02, 0x02, 0x01, 0x00, 0x00, 0x03, 0x01, 0x00, 0x01, 0x03, 0x01, 0x00, 0x02, 0x03, 0x01,
};

const struct ar_boot_profile ar_boot_embedded_profile = {
    .defaults = defaults,
    .defaults_length = sizeof defaults,
    .pins = pins,
    .pins_length = sizeof pins,
    .plan = NULL,
    .plan_length = 0,
    .fch_keys = fch_keys,
    .keys_length = sizeof fch_keys,
    .flash_parts = NULL,  /* no table: identify by SFDP */
    .flash_part_count = 0,
    .io_domains = 2,     /* stock: bank 2's domain */
};
