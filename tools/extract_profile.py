#!/usr/bin/env python3
"""Extract the board-profile data (register defaults, pins, FCH keys) from
an AR8030 firmware image and write a board-profile.c file.

Usage:
  python3 extract_profile.py IMAGE [--out board-profile.c] [--name NAME]

The image is a bb_demo_*.img file. The script searches the firmware section
(offset 0x4600) for a run of exactly 704 valid 8-byte register-default
records, extracts the 40-byte pin table and the 100-byte FCH-key table,
and writes a C source that defines ar_boot_embedded_profile.

The data is pure hardware configuration (register offsets and values the
chip needs at boot), reverse engineered from the stock image.
"""
import struct, sys, os, hashlib

def find_defaults(fw, need=704):
    """Find a contiguous run of exactly `need` valid register-default records."""
    # Each record: offset_le16 (<=0xdfc, %4==0), pad_le16 (==0), value_le32
    best = []
    i = 0
    while i <= len(fw) - need * 8:
        count = 0
        nonzero = 0
        for j in range(need):
            pos = i + j * 8
            off = struct.unpack_from('<H', fw, pos)[0]
            pad = struct.unpack_from('<H', fw, pos + 2)[0]
            if off <= 0xdfc and off % 4 == 0 and pad == 0:
                count += 1
                if off > 0:
                    nonzero += 1
            else:
                break
        if count == need and nonzero > 500:
            best.append((i, nonzero))
        i += 4 if count < need else (need * 8)
    return best

def find_pins(fw):
    """Find 10 x 4-byte pin records: bank(0-1), pin(0-4), value(0-15), 0."""
    candidates = []
    for i in range(0, len(fw) - 40, 4):
        ok = True
        for j in range(10):
            b, p, v, z = fw[i + j*4], fw[i + j*4 + 1], fw[i + j*4 + 2], fw[i + j*4 + 3]
            if b > 1 or p > 4 or v > 15 or z != 0:
                ok = False
                break
        if ok:
            # Check uniqueness of bank*5+pin
            seen = set()
            unique = True
            for j in range(10):
                key = fw[i + j*4] * 5 + fw[i + j*4 + 1]
                if key in seen:
                    unique = False
                    break
                seen.add(key)
            if unique and len(seen) == 10:
                candidates.append(i)
    return candidates

def find_fch_keys(fw):
    """Find 25 x 4-byte FCH encoding keys. These are loaded at runtime from
    the candidate command (operation >= 67, offset 168). They may also appear
    as a constant table in the firmware. Look for 25 non-zero LE32 values
    where each is unique."""
    candidates = []
    for i in range(0, len(fw) - 100, 4):
        keys = [struct.unpack_from('<I', fw, i + j*4)[0] for j in range(25)]
        if all(k != 0 for k in keys) and len(set(keys)) == 25:
            # Check they look like encoding keys (reasonable bit patterns)
            if all(k < 0x10000000 for k in keys):
                candidates.append((i, keys))
    return candidates


def main():
    import argparse
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('image', help='bb_demo_*.img firmware file')
    p.add_argument('--out', default=None, help='output .c file (default: board-profile.c next to the image)')
    p.add_argument('--name', default=None, help='profile name for comments')
    p.add_argument('--dump', action='store_true', help='just dump what was found, no .c output')
    args = p.parse_args()

    with open(args.image, 'rb') as f:
        data = f.read()

    sha = hashlib.sha256(data).hexdigest()
    basename = os.path.basename(args.image)
    print(f"Image: {basename} ({len(data)} bytes)")
    print(f"SHA256: {sha}")

    # Check header
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != 0x41528030:
        sys.exit(f"Not an AR8030 image (magic 0x{magic:08x})")

    fw = data[0x4600:]
    fw_size = struct.unpack_from('<I', data, 0x1c)[0]
    print(f"Firmware section: {fw_size} bytes at file offset 0x4600")

    # Config section (embedded JSON)
    cfg_size = struct.unpack_from('<I', data, 0x2c)[0]
    cfg = data[0x600:0x600 + cfg_size]
    print(f"Config section: {cfg_size} bytes")

    # 1. Find register defaults
    print(f"\n--- Register defaults (looking for 704 x 8B records) ---")
    defaults = find_defaults(fw)
    if not defaults:
        print("  NOT FOUND - trying with lower threshold...")
        # Try with fewer non-zero
        for thresh in [400, 300, 200, 100]:
            for i in range(0, len(fw) - 704*8, 4):
                count = 0
                nonzero = 0
                for j in range(704):
                    pos = i + j * 8
                    if pos + 8 > len(fw):
                        break
                    off = struct.unpack_from('<H', fw, pos)[0]
                    pad = struct.unpack_from('<H', fw, pos + 2)[0]
                    if off <= 0xdfc and off % 4 == 0 and pad == 0:
                        count += 1
                        if off > 0:
                            nonzero += 1
                    else:
                        break
                if count == 704 and nonzero > thresh:
                    defaults.append((i, nonzero))
                    break
            if defaults:
                print(f"  Found with threshold {thresh}")
                break

    for start, nz in defaults:
        print(f"  Found at fw+0x{start:x} (load 0x{0x204000+start:08x}): {nz}/704 non-zero offsets")
        # Show some samples
        for j in [0, 1, 2, 350, 701, 702, 703]:
            off = struct.unpack_from('<H', fw, start + j*8)[0]
            val = struct.unpack_from('<I', fw, start + j*8 + 4)[0]
            print(f"    [{j:3d}] BB+0x{off:03x} = 0x{val:08x}")

    # 2. Find pin records
    print(f"\n--- Pin records (looking for 10 x 4B records) ---")
    pins = find_pins(fw)
    for pi in pins[:5]:
        print(f"  Found at fw+0x{pi:x} (load 0x{0x204000+pi:08x}):")
        for j in range(10):
            b, p, v, z = fw[pi+j*4], fw[pi+j*4+1], fw[pi+j*4+2], fw[pi+j*4+3]
            print(f"    bank={b} pin={p} value={v:2d} (0x{v:x})")

    # 3. Find FCH keys
    print(f"\n--- FCH encoding keys (looking for 25 x 4B values) ---")
    fch = find_fch_keys(fw)
    for fi, keys in fch[:3]:
        print(f"  Candidate at fw+0x{fi:x} (load 0x{0x204000+fi:08x}):")
        for j in range(min(5, len(keys))):
            print(f"    [{j:2d}] 0x{keys[j]:08x}")
        print(f"    ... ({len(keys)} total)")

    if args.dump:
        return

    # Generate C output
    if not defaults:
        sys.exit("Cannot generate profile: no register defaults found")

    out_path = args.out or os.path.join(os.path.dirname(args.image) if '/' in args.image else '.', 'board-profile.c')
    profile_name = args.name or basename.replace('.img', '')

    dstart = defaults[0][0]
    def_data = fw[dstart:dstart + 704*8]

    pin_data = fw[pins[0]:pins[0]+40] if pins else None
    key_data = None
    if fch:
        ki = fch[0][0]
        key_data = fw[ki:ki+100]

    with open(out_path, 'w') as f:
        f.write(f"/* Board profile extracted from {basename}\n")
        f.write(f" * SHA256: {sha}\n")
        f.write(f" * Register defaults, pin configuration and flash identification.\n")
        f.write(f" * FCH encoding keys are supplied at runtime by the host backend.\n")
        f.write(f" * Reverse engineered from the stock firmware. */\n")
        f.write(f'#include "ar_boot.h"\n\n')

        # Defaults
        f.write(f"static const uint8_t defaults[{704*8}] = {{\n")
        for i in range(704):
            rec = def_data[i*8:(i+1)*8]
            off = struct.unpack_from('<H', rec, 0)[0]
            val = struct.unpack_from('<I', rec, 4)[0]
            hex_bytes = ', '.join(f'0x{b:02x}' for b in rec)
            f.write(f"    {hex_bytes},  /* [{i:3d}] BB+0x{off:03x} = 0x{val:08x} */\n")
        f.write(f"}};\n\n")

        # Pins
        if pin_data:
            f.write(f"static const uint8_t pins[40] = {{\n")
            for i in range(10):
                rec = pin_data[i*4:(i+1)*4]
                f.write(f"    {rec[0]}, {rec[1]}, {rec[2]:2d}, 0,  /* bank {rec[0]} pin {rec[1]} */\n")
            f.write(f"}};\n\n")
        else:
            f.write("/* Pin records not found in this image. */\n")
            f.write("static const uint8_t pins[40] = {0};\n\n")

        # FCH keys - actually these come from the host at runtime, not the profile
        f.write("/* FCH encoding keys are supplied by the host backend's candidate\n")
        f.write(" * command (operation >= 67, offset 168 in the channel setup). The\n")
        f.write(" * profile's fch_keys pointer can be NULL if keys are host-supplied. */\n\n")

        # Flash parts
        f.write("static const struct ar_flash_part flash_parts[] = {\n")
        f.write("    /* Populated from the chip's SFDP or a known-device table. */\n")
        f.write("    {0},  /* placeholder */\n")
        f.write("};\n\n")

        # The profile struct
        f.write("const struct ar_boot_profile ar_boot_embedded_profile = {\n")
        f.write("    .defaults = defaults,\n")
        f.write(f"    .defaults_length = sizeof defaults,\n")
        if pin_data:
            f.write("    .pins = pins,\n")
            f.write("    .pins_length = sizeof pins,\n")
        else:
            f.write("    .pins = pins,\n")
            f.write("    .pins_length = 40,\n")
        f.write("    .plan = NULL,  /* loader-based: JSON config supplies the radio plan */\n")
        f.write("    .plan_length = 0,\n")
        f.write("    .fch_keys = NULL,  /* host-supplied at runtime */\n")
        f.write("    .keys_length = 0,\n")
        f.write("    .flash_parts = flash_parts,\n")
        f.write("    .flash_part_count = sizeof flash_parts / sizeof flash_parts[0],\n")
        f.write("};\n")

    print(f"\nWrote {out_path}")
    print(f"  defaults: {704} records from fw+0x{dstart:x}")
    if pin_data:
        print(f"  pins: 10 records from fw+0x{pins[0]:x}")
    if key_data:
        print(f"  fch_keys: found but NOT embedded (host-supplied at runtime)")


if __name__ == '__main__':
    main()
