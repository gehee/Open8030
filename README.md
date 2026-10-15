# Open8030

Open-source radio firmware for **AR8030-based video receivers (VRX)**, built on
Zephyr, with potential support for AR8030-based transmitters (VTX).

**Only the Ascent VRX has been tested**, with matched 18.21.10 inputs and the
original receiver Linux software and transmitter. Other boards may need different
profiles and loading procedures. See [status and remaining work](docs/status.md).

## Setup

Use Linux x86_64 with Python 3.12+ and CMake 3.20+. On Debian/Ubuntu with suitable
package versions:

```sh
sudo apt-get install build-essential cmake git python3 python3-venv \
  device-tree-compiler zlib1g-dev xz-utils file wget openssh-client
```

Place the matched private board profile at `.local/board-profile.c`. It defines
`ar_boot_embedded_profile` from [ar_boot.h](include/ar_boot.h); it is not supplied
and there is no public generation procedure yet. Without it, you can still run
[offline tests and QEMU](#development).

## Build and run on the Ascent VRX

Start from working stock video. Replace `YOUR_VRX_IP` with the receiver's address;
root SSH access and daemon TCP port 50000 must be available.

```sh
# First build: install pinned dependencies and build the image.
./open8030 build --prepare

# Copy the image, stop stock services and leave Open8030 running.
./open8030 run YOUR_VRX_IP
```

No stock firmware image or receiver connection is needed to build. The packager
generates the container header and reserves an empty configuration region; the
receiver's original loader supplies its external JSON configuration at startup.
The private board profile is still required for RF operation.

Later builds need only `./open8030 build`. It builds the runtime and bootstrap,
packages `.local/open8030.img`, and verifies it against [baseline.json](baseline.json).
Repeated builds replace the generated image only after packaging succeeds.
Dependencies are pinned in [toolchain.json](toolchain.json) and
[requirements-build.txt](requirements-build.txt).

`run` creates a temporary directory on the receiver, copies the image and control
script, checks the original software/configuration, reloads the SDIO driver and
restarts the original video services. It reports the log directory and checks
replacement identity and RF link, then leaves Open8030 running until `stop` or
reboot. No separate keep/accept command is needed. **Check fresh camera video**
after startup; identity/link checks do not verify video. For host capture, see
[video verification](docs/status.md#video-verification).

If startup or the identity/link check fails, automatic rollback remains armed
and attempts to restore stock within the five-minute trial window. Stock firmware
files and boot scripts are never replaced.

Check status or return to stock:

```sh
./open8030 status YOUR_VRX_IP
./open8030 stop YOUR_VRX_IP
```

Run `stop` after startup has completed. Verify fresh stock video afterward.
Rebooting the receiver also restores stock. This is RAM-only operation;
persistent installation is not implemented.

If the camera area stays black despite live overlays, reboot into stock and
confirm a visible scene before running Open8030 again.

The image, private inputs and saved session details stay in ignored `.local/`.
Use the same checkout and receiver address for `run` and `stop` so the
helper can find its session. The packaged image contains private board-profile
data; do not publish it. These commands target the tested Ascent VRX baseline
and reject mismatched images or receiver software.

## Development

```sh
./open8030 test             # Native firmware suites and offline helper checks.
./open8030 qemu --prepare   # Build and exercise the protocol in QEMU.
```

QEMU needs no private profile and does not simulate RF or display camera video.
Omit `--prepare` after dependencies are installed. Build, test and QEMU commands
operate locally, except explicitly requested dependency downloads. Use `./open8030 --help` for the available commands.

Sources are in `src/` and `include/`; platform support is in `boards/`, `soc/` and
`dts/`; the bootstrap is in `bootstrap/`; tests are in `tests/`. `tools/` contains
the internal build and VRX control implementation. Generated outputs stay in `.local/`.

## Documentation

- [Architecture](docs/architecture.md): components, startup, memory layout and data flow.
- [Configuration and parameters](docs/configuration.md): inputs, supported JSON fields and CLI options.
- [Operation and troubleshooting](docs/operation.md): session behavior, logs, status and testing.
- [Validation status](docs/status.md): hardware coverage, limitations and HDMI capture instructions.

## License

Original code and documentation use the [MIT License](LICENSE).
See [third-party dependencies](THIRD_PARTY.md) for dependency and private-input boundaries.
