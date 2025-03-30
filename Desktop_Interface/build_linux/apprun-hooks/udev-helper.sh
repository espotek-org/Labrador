#!/bin/sh

if [ "x$this_dir" = "x" ]; then
    this_dir="$(readlink -f "$(dirname "$0")")"
fi

HELPER=$(mktemp "/tmp/labrador-udev-helperXXXXXX")
RULES="69-labrador.rules"
if [ ! -f "/etc/udev/rules.d/$RULES" ] \
&& [ ! -f "/run/udev/rules.d/$RULES" ] \
&& [ ! -f "/lib/udev/rules.d/$RULES" ] \
&& [ -f "$this_dir/lib/udev/rules.d/$RULES" ]; then
    cat > "$HELPER" <<EOHelper
#!/bin/sh
set -e
mkdir -p /run/udev/rules.d
cat > "/run/udev/rules.d/$RULES" <<\EORules
$(<"$this_dir/lib/udev/rules.d/$RULES")
EORules
udevadm control --reload-rules && udevadm trigger --subsystem-match=usb
EOHelper
    chmod u+x "$HELPER"

    echo "Installing udev rules to /run/udev/rules.d/$RULES"
    sudo true && sudo "$HELPER"
    sudo true || pkexec --disable-internal-agent "$HELPER" || true

    rm "$HELPER"
fi
