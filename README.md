# Dell 1320c Native Driver

[![Build](https://github.com/biosed/dell-1320c-cups-driver/actions/workflows/build.yml/badge.svg)](https://github.com/biosed/dell-1320c-cups-driver/actions/workflows/build.yml)

Native rebuild of the Dell 1320c / Fuji Xerox DocuPrint C525A CUPS filter chain.

Builds on Linux (x86_64, aarch64) and macOS (arm64).

This tree contains the working native path extracted from a larger reverse-engineering workspace and reduced to the distributable sources needed to build and install the driver.

Included filters:

- `FXM_PF`
- `FXM_MF`
- `FXM_PS2PM` (shell script)
- `FXM_PM2FXR`
- `FXM_SBP`
- `FXM_PR`
- `FXM_CC`
- `FXM_ALC`
- `FXM_HBPL`

`FXM_HBPL` is built from the working `fxr2hbpl` implementation and installed under the expected Dell filter name.

## Build from source

### Linux

Dependencies: C compiler, CUPS development headers, Ghostscript.

Debian/Ubuntu:
```bash
sudo apt-get install build-essential libcups2-dev ghostscript
```

Fedora:
```bash
sudo dnf install gcc make cups-devel ghostscript
```

Then:
```bash
make
sudo make install
```

Installs filters to `/opt/Dell1320/filter` and PPD to `/usr/share/ppd/Dell/Dell-1320c.ppd`.

### macOS

Dependencies (via Homebrew):
```bash
brew install cups ghostscript
```

Then:
```bash
make
sudo make install
```

Installs filters to `/Library/Printers/Dell/filter` and PPD to `/Library/Printers/PPDs/Dell/Dell-1320c.ppd`.

### Pre-built binaries

Download the tarball for your platform from the [latest release](https://github.com/biosed/dell-1320c-cups-driver/releases/latest), extract it, and run `sudo make install` from the extracted directory.

### Custom paths

```bash
make PREFIX=/custom/filter/root PPD_DIR=/custom/ppd/dir
```

## Add the printer

### Linux

Open your desktop printer settings or the CUPS web UI, add the printer, and select the Dell 1320c PPD.

Or from the command line:
```bash
sudo lpadmin -p Dell1320c -E \
  -v "usb://Dell/Color%20Laser%201320c?serial=YOUR_SERIAL" \
  -P /usr/share/ppd/Dell/Dell-1320c.ppd
sudo lpadmin -p Dell1320c -o Option1=1Tray-S -o FXInputSlot=1stTray-S
```

### macOS

Open **System Settings > Printers & Scanners**, add the printer, and select the Dell 1320c PPD when prompted for a driver.

Or from the command line:
```bash
sudo lpadmin -p Dell1320c -E \
  -v "usb://Dell/Color%20Laser%201320c?serial=YOUR_SERIAL" \
  -P /Library/Printers/PPDs/Dell/Dell-1320c.ppd
sudo lpadmin -p Dell1320c -o Option1=1Tray-S -o FXInputSlot=1stTray-S
```

## Validation

This driver was validated in two ways.

### 1. Byte-for-byte oracle comparisons

The native filters were compared against the vendor Linux i386 filters on the same intermediate inputs.

Key verified areas:

- `FXM_ALC`
  - structured synthetic cases
  - random stress cases
  - real captured color job from the working CUPS pipeline
- `FXM_PM2FXR`
  - header fields and body checked against vendor output on direct test input
- `FXM_HBPL` replacement (`fxr2hbpl` / `FXM_HBPL`)
  - output after `@PJL ENTER LANGUAGE=HBPL` matched the vendor output byte-for-byte on the same input

For the HBPL stage, the remaining differences from the vendor output are in PJL metadata fields such as `DATE`, `TIME`, and `@HOAD`, which are environment/time dependent rather than raster-content dependent.

### 2. Real printer validation

The native chain was exercised on an actual Dell 1320c printer.

Confirmed working:

- color print job
- monochrome print job
- standard Linux CUPS test page

The full active print path was sampled during printing and completed without `qemu-i386-static` present in the runtime process list.

## Scope and caveats

- `FXM_PF` in this tree is a clean-room implementation based on observed behavior, not recovered vendor source.
- `FXM_HBPL` in this tree is the working clean-room `fxr2hbpl` implementation under the expected Dell filter name.
- `FXM_PS2PM` remains a shell script wrapper around Ghostscript.
- The included PPD is the single canonical PPD for this repo.
- This tree is intended to contain only the working native path, not the reverse-engineering artifacts.
- Linux desktop users should treat this like a normal CUPS driver package: install first, then add the printer in the GUI.

## Notes

- Tray defaults are defined by the PPD and are not changed by the build.
- The active working setup uses the Dell PPD default `FXInputSlot=1stTray-S`.
