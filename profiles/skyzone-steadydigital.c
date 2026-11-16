/* Skyzone SteadyDigital, SDIO receiver (DEV).
 * Host: AR9311 (artosyn_sdio.ko), SDIO ID 4152:8031.
 * From the receiver's own stock firmware (bb_demo_gnd.img, c401 generation),
 * not the Ascent ground image's: its board init picks board type 5
 * ("c401_pro" + 2TX_STBC), whose pin list and RF front-end tail set banks 2
 * and 3 too, and its I/O domain setup selects bank 2's. The Ascent gnd_3v3
 * pin list alone linked at MCS 2 at best and lost the link. No SPI flash on
 * this board: build with -DAR_FACTORY_OPTIONAL.
 * Reverse engineered data, not vendor executable code.
 * Provenance and differences from other profiles: profiles/README.md.
 */
#include "ar_boot.h"

#include "bb_defaults.inc"

static const uint8_t pins[80] = {
    /* bank, pin, mux, 0 - board type 5's pin list, then its RF front-end tail */
    0, 0, 0, 0,
    0, 1, 0, 0,
    0, 2, 0, 0,
    0, 3, 0, 0,
    0, 4, 0, 0,
    1, 0, 0, 0,
    1, 3, 1, 0,
    1, 4, 1, 0,
    2, 4, 9, 0,
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

/* FCH encoding keys: the MCS -> frame-control-header key table from the box's
 * stock firmware (it differs from the Ascent images'). */
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
