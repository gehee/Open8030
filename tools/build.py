#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and package Open8030 with pinned local dependencies."""
import json
import hashlib
import shutil
import struct
import tempfile
import urllib.request
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import venv

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / ".local"
TOOLS = LOCAL / "tools"
PINS = json.loads((ROOT / "toolchain.json").read_text())
SDK = TOOLS / PINS["sdk"]["directory"]
PYTHON = LOCAL / "zephyr-venv/bin/python"
SOURCE = LOCAL / "zephyr"


def checksum(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def archive_for(spec, tools):
    archive = tools / spec["archive"]
    if not archive.exists():
        staging = archive.with_suffix(archive.suffix + ".download")
        print(f"Downloading {spec['url']}", flush=True)
        with urllib.request.urlopen(spec["url"], timeout=120) as response, staging.open("xb") as output:
            shutil.copyfileobj(response, output)
        if checksum(staging) != spec["sha256"]:
            raise ValueError(f"SHA-256 mismatch: retained {staging}")
        staging.rename(archive)
    if checksum(archive) != spec["sha256"]:
        raise ValueError(f"SHA-256 mismatch: {archive}")
    return archive


def run(command, **kwargs):
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def extract(spec, destination):
    archive = archive_for(spec, TOOLS)
    if not (destination / spec["directory"]).exists():
        with tarfile.open(archive) as source:
            for member in source.getmembers():
                relative = Path(member.name)
                if relative.is_absolute() or ".." in relative.parts or relative.parts[0] != spec["directory"]:
                    raise ValueError("Unexpected SDK archive path")
            source.extractall(destination, filter="data")


def prepare():
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise ValueError("Pinned tool archives are Linux x86_64 only")
    TOOLS.mkdir(parents=True, exist_ok=True)
    extract(PINS["sdk"], TOOLS)
    extract(PINS["riscv_toolchain"], SDK)
    qemu = SDK / "hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-riscv32"
    if not qemu.exists():
        run(["bash", SDK / "setup.sh", "-h"])
    if not PYTHON.exists():
        venv.create(PYTHON.parents[1], with_pip=True)
    run([PYTHON, "-m", "pip", "install", "-r", ROOT / "requirements-build.txt"])
    if not SOURCE.exists():
        run(["git", "clone", "--depth", "1", "--branch", PINS["zephyr_tag"],
             "https://github.com/zephyrproject-rtos/zephyr.git", SOURCE])
    verify_source()
    if not (LOCAL / ".west").exists():
        run([PYTHON.parent / "west", "init", "-l", "zephyr"], cwd=LOCAL)


def verify_source():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    if commit != PINS["zephyr_commit"]:
        raise ValueError("Existing Zephyr source differs from the pinned commit; not overwritten")


def build(board, cflags, boot_profile=None):
    verify_source()
    env = dict(os.environ)
    env.pop("ZEPHYR_SDK_INSTALL_DIR", None)
    env.update(PATH=str(PYTHON.parent) + os.pathsep + env.get("PATH", "/usr/bin:/bin"),
               ZEPHYR_BASE=str(SOURCE), ZEPHYR_TOOLCHAIN_VARIANT="cross-compile",
               CROSS_COMPILE=str(SDK / "riscv64-zephyr-elf/bin/riscv64-zephyr-elf-"))
    directory = LOCAL / ("build-qemu-ready" if board == "qemu_riscv32" else "build-ar8030-bringup")
    if boot_profile:
        boot_profile = boot_profile.resolve(strict=True)
        if board != "ar8030_bringup" or not boot_profile.is_relative_to(LOCAL.resolve()) or boot_profile.suffix != ".c":
            raise ValueError("Boot profile must be generated private .local/ C data for ar8030_bringup")
    run(["cmake", "-GNinja", "-S", ROOT, "-B", directory,
         f"-DBOARD={board}", f"-DPython3_EXECUTABLE={PYTHON}", f"-DCMAKE_C_FLAGS={cflags}",
         f"-DAR8030_BOOT_PROFILE_SOURCE={boot_profile or ''}"], env=env)
    run(["cmake", "--build", directory, "-j4"], env=env)



def build_bootstrap():
    output = ROOT / '.local/build-ar8030-bootstrap'
    output.mkdir(parents=True, exist_ok=True)
    prefix = SDK / 'riscv64-zephyr-elf/bin/riscv64-zephyr-elf-'
    elf, binary = output / 'bootstrap.elf', output / 'bootstrap.bin'
    command = [str(prefix)+'gcc', '-march=rv32imc_zicsr', '-mabi=ilp32', '-Os',
               '-std=c11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin',
               '-fno-stack-protector', '-fno-pic', '-msmall-data-limit=0', '-nostdlib',
               '-Wl,--build-id=none', '-Wl,--no-relax',
               '-T', ROOT/'bootstrap/bootstrap.ld', ROOT/'bootstrap/start.S',
               ROOT/'bootstrap/bootstrap.c', '-o', elf]
    subprocess.run([str(x) for x in command], check=True)
    subprocess.run([str(prefix)+'objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
    data=binary.read_bytes()
    if not 0 < len(data) <= 0x5f8: raise ValueError('Bootstrap overlaps ROM scratch')
    sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
             for p in (ROOT/'bootstrap').iterdir() if p.is_file()}
    result={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'load_address':'0x0a098a00',
            'sources':sources,'compiler':subprocess.check_output([str(prefix)+'gcc','--version'],text=True).splitlines()[0],
            'hardware_verified':False,'limitations':['Source-authored matched bootstrap companion, not complete RF replacement.',
              'PMA/control/scratch contracts require independent original-byte and hardware validation.',
              'UART debug output omitted; hardware and timer wait failure paths are bounded.']}
    (output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))



MAGIC = 0x41528030
LAYOUT_SOURCE = (
    "https://github.com/henkwiedig/ar8030-transport/blob/"
    "bb6e14f1f0952f85aa5ab6ded14d3782cf6dd372/kmod/artosyn_sdio.c"
)
FIELDS = (
    "header_checksum", "image_version", "flag", "boot_info",
    "spl_load_address", "spl_length", "troot_load_address", "troot_length",
    "signature_load_address", "signature_length", "spl_dtb_offset",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value, boundary=512):
    return (value + boundary - 1) & ~(boundary - 1)


def parse_image(data):
    if len(data) < 52:
        raise ValueError("truncated AR8030 header")
    magic, kind, size = struct.unpack_from("<IHH", data)
    if magic != MAGIC or kind != 1:
        raise ValueError("unsupported image magic/type")
    if not 52 <= size <= len(data):
        raise ValueError("invalid header length")
    header = dict(zip(FIELDS, struct.unpack_from("<11I", data, 8)))
    header.update(magic=magic, image_type=kind, header_length=size)
    if not header["spl_length"]:
        raise ValueError("empty main payload")
    # The matched Linux loader independently checks the troot and configuration
    # lengths. It advances only over present segments; this is a file/transfer
    # layout contract, not proof that the ROM accepts an omitted bootstrap.
    names = (("troot", "signature", "spl") if header["troot_length"] else
             ("signature", "spl") if header["signature_length"] else ("spl",))
    offset = align(size)
    segments = {}
    spans = []
    for name in names:
        length = header[f"{name}_length"]
        address = header[f"{name}_load_address"]
        if offset + length > len(data):
            raise ValueError(f"{name}: segment exceeds file")
        if address + length > 0x100000000:
            raise ValueError(f"{name}: RV32 address overflow")
        if length:
            for start, end in spans:
                if address < end and start < address + length:
                    raise ValueError("overlapping download destinations")
            spans.append((address, address + length))
        payload = data[offset:offset + length]
        segments[name] = {"offset": offset, "bytes": length,
                          "load_address": address, "sha256": digest(payload),
                          "data": payload}
        offset += align(length)
    # The last payload's transfer padding may be absent from the file.
    payload_end = max(s["offset"] + s["bytes"] for s in segments.values())
    return header, segments, {"aligned_end": offset, "payload_end": payload_end,
                              "bytes_after_payload": len(data) - payload_end,
                              "bytes_after_aligned_end": max(0, len(data) - offset)}


def make_image(data, bootstrap):
    """Generate a loader container; external receiver JSON supplies configuration."""
    if not data or len(data) > 0xac000:
        raise ValueError('Runtime does not fit the bounded RAM region')
    if not 0 < len(bootstrap) <= 0x5f8:
        raise ValueError('Bootstrap overlaps ROM scratch')
    payload = data + bytes((-len(data)) % 512)
    header = bytearray(64)
    struct.pack_into('<IHH', header, 0, MAGIC, 1, 64)
    struct.pack_into('<11I', header, 8, 0, 1, 0, 0,
                     0x204000, len(payload), 0x0a098a00, len(bootstrap),
                     0x200000, 16384, 0)
    payload_checksum = sum(struct.unpack(f'<{len(payload) // 8}Q', payload)) & 0xffffffffffffffff
    struct.pack_into('<Q', header, 0x34, payload_checksum)
    struct.pack_into('<I', header, 8, sum(struct.unpack('<16I', header)) & 0xffffffff)
    image = bytearray(header)
    image.extend(bytes(512 - len(image)))
    image.extend(bootstrap)
    image.extend(bytes(align(len(image)) - len(image)))
    # The driver writes external bb_config_gnd.json here after the payload transfer.
    # Reserve the same region without embedding any stock configuration bytes.
    image.extend(bytes(16384))
    image.extend(payload)
    return bytes(image)


def package(binary, bootstrap, output):
    if output.exists() or output.with_suffix('.manifest.json').exists():
        raise FileExistsError('Refusing to overwrite candidate or its manifest')
    baseline = json.loads((ROOT / 'baseline.json').read_text())
    data, boot = binary.read_bytes(), bootstrap.read_bytes()
    for name, content in (('runtime', data), ('bootstrap', boot)):
        if digest(content) != baseline[name]['sha256']:
            raise ValueError(f'{name}: not the accepted baseline input')
    result = make_image(data, boot)
    header, spans, _ = parse_image(result)
    expected = baseline['generated_container']
    if len(result) != expected['bytes'] or digest(result) != expected['sha256']:
        raise ValueError('Generated container does not match the recorded layout')
    with output.open('xb') as file:
        file.write(result)
    receipt = {
        'input_sha256': digest(data), 'input_bytes': len(data),
        'payload_alignment': 512, 'payload_bytes': spans['spl']['bytes'],
        'container_sha256': digest(result), 'container_bytes': len(result),
        'entry': hex(header['spl_load_address']), 'header_checksum': header['header_checksum'],
        'bootstrap_sha256': digest(boot), 'bootstrap_mode': 'source',
        'configuration': 'generated zero-filled 16 KiB region; external loader JSON required',
        'stock_template_required': False, 'hardware_tested_by_this_build': False,
    }
    with output.with_suffix('.manifest.json').open('x') as file:
        file.write(json.dumps(receipt, indent=2) + '\n')
    return receipt



def build_release(profile):
    """Build both components and publish only a fully verified container."""
    baseline = json.loads((ROOT / 'baseline.json').read_text())
    if not profile.is_file() or checksum(profile) != baseline['private_board_profile']['sha256']:
        raise ValueError(f'{profile}: missing or unmatched private input')
    build('ar8030_bringup', '', profile)
    build_bootstrap()
    # Packaging validates before replacing generated outputs, so repeated builds
    # work without requiring the user to remove the previous working image.
    with tempfile.TemporaryDirectory(dir=LOCAL, prefix='package-') as directory:
        staged = Path(directory) / 'open8030.img'
        package(LOCAL / 'build-ar8030-bringup/zephyr/zephyr.bin',
                LOCAL / 'build-ar8030-bootstrap/bootstrap.bin', staged)
        staged.replace(LOCAL / 'open8030.img')
        staged.with_suffix('.manifest.json').replace(LOCAL / 'open8030.manifest.json')
    print(f'Image ready: {LOCAL / "open8030.img"}')
