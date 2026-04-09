#!/usr/bin/env bash
set -euo pipefail

make
sudo make install

cat <<'EOF'

Installed:
  Filters: /opt/Dell1320/filter
  PPD:     /usr/share/ppd/Dell/Dell-1320c.ppd

Next step:
  Add the printer from the CUPS GUI using the Dell 1320c PPD,
  or follow INSTALL.md for the lpadmin command.

EOF
