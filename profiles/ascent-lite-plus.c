/* Board profile: Ascent Lite+ (board_type 472)
 * Transport: SDIO (Hi3516CV610 host)
 * Firmware:  Ascent_H_Sky_18.21.10_bb_demo_sky_cx472.img
 * Role:      AP / air (set by the radio config, not the profile)
 * Sensor:    OS02K10 (1080p100, 864p144)
 * Notes:     Different FEM, no IMU, audio codec present
 *
 * Pin records: from the stock firmware board pinmux init (offline).
 * Reverse engineered from the stock firmware. */
#include "ar_boot.h"

#include "bb_defaults.inc"

static const uint8_t pins[26 * 4] = {
    /* bank, pin, mux, pad - the stock sky_cx472 18.21.10 boot pinmux
     * (0x20529a), all four banks; mux from its pin function table 0x266c2c.
     * Bank 3 pins 4-9 carry the RF front-end controls (0x7f-0x84). */
    0, 1, 0, 0,  /* func 0x19 */
    0, 2, 0, 0,  /* func 0x1a */
    0, 4, 0, 0,  /* func 0x1c */
    1, 0, 7, 0,  /* func 0x09 */
    1, 1, 7, 0,  /* func 0x0c */
    1, 3, 1, 0,  /* func 0x98 */
    1, 4, 1, 0,  /* func 0x97 */
    2, 3, 2, 0,  /* func 0x9e */
    2, 4, 2, 0,  /* func 0x9d */
    2, 0, 2, 0,  /* func 0x4b */
    2, 1, 2, 0,  /* func 0x46 */
    2, 2, 6, 0,  /* func 0x4a */
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
    3, 11, 7, 0,  /* func 0x9c */
    3, 12, 7, 0,  /* func 0x9b */
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
    .io_domains = 4,      /* stock 0x244e80(0): bank 3's domain only */
};
