#!/bin/sh

set -e

appdir=""

show_usage() {
    echo "Usage: $0 --appdir <AppDir>"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --plugin-api-version)
            echo "0"
            exit 0
            ;;
        --appdir)
            appdir="$2"
            shift
            shift
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            echo "Invalid argument: $1"
            echo
            show_usage
            exit 2
            ;;
    esac
done

if [ "x$appdir" = "x" ]; then
    show_usage
    exit 2
fi

echo "Creating AppRun hook file: $appdir/apprun-hooks/linuxdeploy-plugin-udev.sh"
mkdir -p "$appdir/apprun-hooks"
cat > "$appdir/apprun-hooks/linuxdeploy-plugin-udev.sh" <<\EOHook
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
EOHook
