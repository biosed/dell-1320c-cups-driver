#!/usr/bin/env bash
set -euo pipefail

OS="$(uname -s)"
if [ "$OS" = "Darwin" ]; then
  PREFIX="${PREFIX:-/Library/Printers/Dell}"
  PPD_DIR="${PPD_DIR:-/Library/Printers/PPDs/Dell}"
else
  PREFIX="${PREFIX:-/opt/Dell1320}"
  PPD_DIR="${PPD_DIR:-/usr/share/ppd/Dell}"
fi

make
sudo make install PREFIX="$PREFIX" PPD_DIR="$PPD_DIR"

cat <<EOF

Installed:
  Filters: ${PREFIX}/filter
  PPD:     ${PPD_DIR}/Dell-1320c.ppd

Next step:
EOF

if [ "$OS" = "Darwin" ]; then
  cat <<'EOF'
  1. Relax the macOS CUPS sandbox if Ghostscript is installed via Homebrew:
       grep -q "^Sandboxing Relaxed" /etc/cups/cups-files.conf || echo "Sandboxing Relaxed" | sudo tee -a /etc/cups/cups-files.conf
       sudo launchctl kickstart -k system/org.cups.cupsd
  2. Add the printer in System Settings > Printers & Scanners using the Dell 1320c PPD,
     or follow INSTALL.md for the lpadmin command.
EOF
else
  cat <<'EOF'
  Add the printer from the CUPS GUI using the Dell 1320c PPD,
  or follow INSTALL.md for the lpadmin command.
EOF
fi
