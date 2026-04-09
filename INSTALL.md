# Installation

## How printer drivers are usually shipped on Linux

Unlike Windows, CUPS does not usually download and install vendor drivers automatically from the add-printer dialog.

The normal Linux model is:

1. install a package or run an installer that drops:
   - filter binaries into a CUPS-visible location
   - a PPD into the system PPD directory
2. open the CUPS GUI or your desktop printer settings
3. add the printer and select the installed model/PPD

So the closest equivalent to a Windows-style vendor installer is:

- a package (`.deb`, `.rpm`, etc.), or
- an install script that copies the filters and PPD into the correct places

This repository provides the second option.

## Install the driver files

```bash
make check-deps
make
sudo make install
```

That installs:

- filters into `/opt/Dell1320/filter`
- PPD into `/usr/share/ppd/Dell/Dell-1320c.ppd`

## Add the printer from the CUPS GUI

After `make install`, open your printer settings / CUPS web UI and add the printer normally.

When asked for the driver/model:

- choose the Dell 1320c PPD if it appears in the model list, or
- browse to:
  - `/usr/share/ppd/Dell/Dell-1320c.ppd`

Recommended defaults:

- `Option1 = 1Tray-S`
- `FXInputSlot = 1stTray-S`

## Add the printer from the command line

Example:

```bash
sudo lpadmin -p Dell1320c -E \
  -v "usb://Dell/Color%20Laser%201320c?serial=YOUR_SERIAL" \
  -P /usr/share/ppd/Dell/Dell-1320c.ppd

sudo lpadmin -p Dell1320c -o Option1=1Tray-S -o FXInputSlot=1stTray-S
sudo lpoptions -d Dell1320c
```

## Packaging for easier installation

If you want a more Windows-like experience, the next step is to package this repo as:

- a `.deb` for Debian/Ubuntu
- an `.rpm` for Fedora/RHEL/openSUSE

That would let users install the driver first, then just pick it from the printer GUI.
