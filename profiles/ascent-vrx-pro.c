/* Board profile: Ascent VRX Pro
 * Transport: USB (DWC2 device, arlink.ko)
 * Firmware:  Ascent_VRX_Pro_18.21.10_bb_demo_cx485_2PA.img
 * Role:      AP / ground
 * Board:     cx485, 2PA (two power amplifiers)
 * USB ID:    1d6b:8030 (running), 4152:8030 (boot ROM)
 *
 * Pin records: from the stock firmware board pinmux init (offline).
 * Reverse engineered from the stock firmware. */
#include "ar_boot.h"

#include "bb_defaults.inc"

static const uint8_t pins[29 * 4] = {
    /* bank, pin, mux, pad - the stock cx485_2PA 18.21.10 boot pinmux
     * (0x20540a), all four banks; mux from the pin function table 0x270320. */
    0, 0, 4, 0,  /* func 0x9c */
    0, 1, 3, 0,  /* func 0x9a */
    0, 2, 3, 0,  /* func 0x99 */
    0, 3, 2, 0,  /* func 0x9b */
    0, 4, 0, 0,  /* func 0x1c */
    1, 0, 7, 0,  /* func 0x09 */
    1, 1, 7, 0,  /* func 0x0c */
    1, 2, 7, 0,  /* func 0x0d */
    1, 3, 1, 0,  /* func 0x98 */
    1, 4, 1, 0,  /* func 0x97 */
    2, 0, 0, 0,  /* func 0x22 */
    2, 1, 2, 0,  /* func 0x46 */
    2, 2, 6, 0,  /* func 0x4a */
    2, 3, 0, 0,  /* func 0x25 */
    2, 4, 0, 0,  /* func 0x26 */
    2, 5, 4, 0,  /* func 0x47 */
    3, 0, 2, 0x12,  /* func 0x5c, pad 0x12 */
    3, 1, 2, 0x12,  /* func 0x5e, pad 0x12 */
    3, 2, 2, 0x12,  /* func 0x5f, pad 0x12 */
    3, 3, 2, 0x12,  /* func 0x5d, pad 0x12 */
    3, 4, 1, 0,  /* func 0x7f */
    3, 5, 1, 0,  /* func 0x80 */
    3, 6, 1, 0,  /* func 0x81 */
    3, 7, 1, 0,  /* func 0x82 */
    3, 8, 1, 0,  /* func 0x83 */
    3, 9, 1, 0,  /* func 0x84 */
    3, 10, 0, 0,  /* func 0x32 */
    3, 11, 0, 0,  /* func 0x33 */
    3, 12, 0, 0,  /* func 0x34 */
};

/* MCS -> frame-control-header encoding keys, 25 x LE32: the table the stock
 * 18.21.10 firmware itself matches received FCH words against (cx485 image
 * at 0x25bf9c; byte-identical in gnd_3v3, sky_3v3 and sky_cx472). Index =
 * internal MCS (JSON mcs + 2); index 2 is key 0, what an AP at MCS 0 sends. */
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
};
