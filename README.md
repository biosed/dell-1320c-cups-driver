# Dell 1320c Native Driver

Native rebuild of the Dell 1320c / Fuji Xerox DocuPrint C525A CUPS filter chain.

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

## Build

Dependencies:

- C compiler (`gcc` or compatible)
- `pkg-config`
- CUPS development headers/libs (`pkg-config cups`)
- Ghostscript runtime (`gs`) for `FXM_PS2PM`

```bash
make
```

You can also verify prerequisites explicitly:

```bash
make check-deps
```

## Install

```bash
sudo make install
```

Default install paths:

- filters: `/opt/Dell1320/filter`
- PPD: `/usr/share/ppd/Dell/Dell-1320c-native.ppd`

Override with:

```bash
make PREFIX=/custom/filter/root PPD_DIR=/custom/ppd/dir
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
- The included PPD is the native Dell/Fuji-Xerox style PPD, not the older foo2-based adapted PPD.
- This tree is intended to contain only the working native path, not the reverse-engineering artifacts.

## Notes

- Tray defaults are defined by the PPD and are not changed by the build.
- The active working setup uses the Dell PPD default `FXInputSlot=1stTray-S`.
