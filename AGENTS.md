# AGENTS.md

Native CUPS driver for Dell 1320c / Fuji Xerox DocuPrint C525A. Plain C99 + one shell script, no tests, no lint, no package manager.

## Build / install

```bash
make check-deps   # verifies cc, cups-config, gs are present
make              # outputs to bin/
sudo make install
```

- Deps: C compiler, CUPS dev headers (`libcups2-dev` Debian / `cups-devel` Fedora / `cups` via Homebrew on macOS), Ghostscript (`gs`, build-time check and runtime dep of `FXM_PS2PM`).
- Install paths are platform-dependent (see `Makefile`): Linux `PREFIX=/opt/Dell1320` + `PPD_DIR=/usr/share/ppd/Dell`; macOS `/Library/Printers/Dell` + `/Library/Printers/PPDs/Dell`.
- Overrides: `make PREFIX=... PPD_DIR=...`, plus `DESTDIR=` for staging. `VERSION` defaults to `0.1.3` and names the `make dist` tarball (`dist/dell-1320c-cups-driver-<OS>-<ARCH>-v<VERSION>.tar.gz` + `.sha256`). `make dist` includes `Makefile` and `Makefile` supports pre-built binary installs when `src/` is absent.
- `install.sh` detects Linux vs macOS defaults and accepts `PREFIX=`/`PPD_DIR=` env overrides (`make && sudo make install ...`).
- CI (`.github/workflows/build.yml`) only runs `make all` + `make dist` on ubuntu/macos. Release (`release.yml`) triggers on `v*` tags. There is no test or lint step — verify with a build.

## Architecture: CUPS filter chain

PPD (`ppd/Dell-1320c.ppd`, the single canonical PPD) wires the chain:

- Entry: `*cupsFilter: ... /opt/Dell1320/filter/FXM_PF`
- Orchestrator: `*FXMainFilter: .../FXM_MF`, dir `*FXFilterDir`, ordered chain `*FXFilterChain: "FXM_PS2PM, FXM_PM2FXR, FXM_SBP, FXM_PR, FXM_CC, FXM_ALC, FXM_HBPL"`

Flow: `FXM_PF` (saves PS stdin to tempfile, merges `%%BeginFeature: *Key Value` lines into CUPS options, execs `FXMainFilter`) → `FXM_MF` (reads `FXFilterChain`/`FXFilterDir` from PPD, fork/execs each stage piping stdout→stdin) → `FXM_PS2PM` (only script, in `scripts/` not `src/`, wraps Ghostscript) → `FXM_PM2FXR` → `FXM_SBP` → `FXM_PR` → `FXM_CC` → `FXM_ALC` → `FXM_HBPL` (emits PJL/HBPL to backend).

- All filters follow CUPS convention: `filter job-id user title copies options [file]`; read stdin or `argv[6]`.
- Inter-filter format is 44-byte little-endian FXRaster header + payload (each `.c` header documents its field layout; layouts differ slightly per stage — read the stage's struct, don't assume one shared header).
- `FXM_ALC` is the only multi-file target: `src/FXM_ALC.c + src/sq21_simple.c` (decl in `src/sq21_simple.h`).
- Known implementation status (don't "fix" without reason): `FXM_PF` and `FXM_HBPL` are clean-room; `FXM_CC` is an intentional passthrough (vendor color-calibration LUTs not reimplemented, color may be uncalibrated).
- Gotcha: `ppd/Dell-1320c.ppd` in git has `/opt/Dell1320` paths; `make install` and `make dist` rewrite `/opt/Dell1320` -> `$(PREFIX)` in the installed/packaged PPD. `scripts/FXM_PS2PM` augments `PATH` with `/opt/homebrew/bin:/usr/local/bin:/opt/local/bin:/usr/bin` to locate `gs`, and on macOS CUPS still requires `Sandboxing Relaxed` in `/etc/cups/cups-files.conf` for Homebrew `gs`.

## Conventions

- C99 with `-Wall -Wextra`; match existing style (CUPS-filter `main`, explicit `read()` loops, no new dependencies without updating `Makefile` + CI deps + README).
- Keep `src/` (compiled filters) vs `scripts/` (shell wrapper) vs `ppd/` separation; `Makefile` `FILTERS` list and `install` target must stay in sync when adding a filter.
- No test suite. Validation precedent (README): byte-compare filter output against vendor i386 filters on identical intermediate inputs, ignoring time-dependent PJL fields (`DATE`, `TIME`, `@HOAD`); plus real-printer color/mono/test-page jobs with no `qemu-i386-static` in the process list.
- `bin/` and `dist/` are gitignored build outputs; don't commit them.

