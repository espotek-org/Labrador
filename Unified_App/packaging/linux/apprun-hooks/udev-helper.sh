#!/bin/sh
# AppImage AppRun hook: install the Labrador udev rules on first run if they
# aren't already present system-wide, so the board is accessible without root.
# Adapted from the Qt build's build_linux/apprun-hooks/udev-helper.sh.

if [ "x$this_dir" = "x" ]; then
    this_dir="$(readlink -f "$(dirname "$0")")"
fi

RULES="69-labrador.rules"
SRC="$this_dir/usr/lib/udev/rules.d/$RULES"

if [ ! -f "/etc/udev/rules.d/$RULES" ] \
&& [ ! -f "/run/udev/rules.d/$RULES" ] \
&& [ ! -f "/lib/udev/rules.d/$RULES" ] \
&& [ -f "$SRC" ]; then
    HELPER=$(mktemp "/tmp/labrador-udev-helperXXXXXX")
    cat > "$HELPER" <<EOHelper
#!/bin/sh
set -e
mkdir -p /run/udev/rules.d
cat > "/run/udev/rules.d/$RULES" <<\EORules
$(cat "$SRC")
EORules
udevadm control --reload-rules && udevadm trigger --subsystem-match=usb
EOHelper
    chmod u+x "$HELPER"

    echo "Installing udev rules to /run/udev/rules.d/$RULES"
    sudo true && sudo "$HELPER" 2>/dev/null \
        || pkexec --disable-internal-agent "$HELPER" 2>/dev/null \
        || echo "Could not install udev rules automatically; run 'sudo cp $SRC /etc/udev/rules.d/' if the board is not detected."

    rm -f "$HELPER"
fi
