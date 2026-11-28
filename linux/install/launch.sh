#!/bin/sh
# RS Re-ARMed's command in a packaged build (usr/bin/rs-rearmed), and the AppImage's AppRun: rs_install.py, with
# the system's Python 3. No arguments: the game, after a setup when there has been none. See rs_install.py.
here=$(dirname "$(readlink -f "$0")")
if ! command -v python3 >/dev/null 2>&1; then
    msg="RS Re-ARMed needs Python 3, which is not on this system. Install it with your distribution's package manager, then start RS Re-ARMed again."
    echo "rs-rearmed: $msg" >&2
    if [ ! -t 2 ]; then
        if command -v zenity >/dev/null 2>&1; then
            zenity --error --title "RS Re-ARMed" --width 480 --text "$msg"
        elif command -v kdialog >/dev/null 2>&1; then
            kdialog --title "RS Re-ARMed" --error "$msg"
        fi
    fi
    exit 127
fi
exec python3 -I "$here/../share/rs-rearmed/rs_install.py" "$@"
