## Summary

First public Linux `aarch64` release of the native Dell 1320c / Fuji Xerox DocuPrint C525A CUPS driver.

This release includes a native filter chain for:

- `FXM_PF`
- `FXM_MF`
- `FXM_PS2PM` (shell script)
- `FXM_PM2FXR`
- `FXM_SBP`
- `FXM_PR`
- `FXM_CC`
- `FXM_ALC`
- `FXM_HBPL`

## Validation

This release was validated in two ways.

### Byte comparisons

Native filters were compared against the vendor Linux i386 filters on the same intermediate inputs.

Verified areas include:

- `FXM_ALC`
  - structured synthetic cases
  - random stress cases
  - real captured color job from the working CUPS pipeline
- `FXM_PM2FXR`
  - header fields and body checked against vendor output
- `FXM_HBPL`
  - output after `@PJL ENTER LANGUAGE=HBPL` matched the vendor output byte-for-byte on the same input

### Real printer testing

Tested successfully on a real Dell 1320c printer:

- color print job
- monochrome print job
- standard Linux CUPS test page

The active working print path completed without `qemu-i386-static` present in the runtime process list.

## Included assets

- `dell-1320c-cups-driver-linux-aarch64-v0.1.0.tar.gz`
- `dell-1320c-cups-driver-linux-aarch64-v0.1.0.tar.gz.sha256`

## Notes

- This release targets Linux `aarch64`.
- Ghostscript (`gs`) is required at runtime for `FXM_PS2PM`.
- The included PPD is `Dell-1320c.ppd`.
- Recommended defaults:
  - `Option1 = 1Tray-S`
  - `FXInputSlot = 1stTray-S`
