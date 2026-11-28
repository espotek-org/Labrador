#!/usr/bin/env bash
# Builds Unified_App/Labrador-for-Linux-<arch>.deb: the GUI and the headless
# labrador-cli, fully dynamic (dpkg manages their dependencies), with the
# firmware hex, udev rules, icon and .desktop entry; then installs it into
# this container and smoke-tests labrador-cli from PATH.  Runs as root in a
# container prepared by install-build-deps.sh deb, on the architecture it
# builds for.
#
#   build-deb.sh x86_64|arm64|i386 <version>
set -euxo pipefail

ARCH=$1
VERSION=$2
cd "$(dirname "$(readlink -f "$0")")/../.."
. packaging/linux/arch.sh

# CMake embeds the commit in the app, reading it quietly, so a git that
# refuses the checkout would leave it empty.
git rev-parse HEAD

echo "::group::Build"
cmake --preset linux -DLABRADOR_VERSION="$VERSION"
cmake --build --preset linux
check_elf build/linux/labrador build/linux/cli/labrador-cli
echo "::endgroup::"

echo "::group::Build .deb"
DEB="Labrador-for-Linux-$ARCH.deb"
PKG=pkgroot
rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" \
         "$PKG/usr/lib/labrador/assets" \
         "$PKG/usr/bin" \
         "$PKG/lib/udev/rules.d" \
         "$PKG/usr/share/applications" \
         "$PKG/usr/share/icons/hicolor/256x256/apps"
# Real binary + assets in a private dir; /usr/bin/labrador is a symlink.
# SDL_GetBasePath() resolves /proc/self/exe to the real dir, so assets are
# found next to the binary at /usr/lib/labrador/assets.
install -m 0755 build/linux/labrador "$PKG/usr/lib/labrador/labrador"
# labrador-cli lives beside the GUI so both find the same assets (its
# update-firmware flashes the bundled hex); the /usr/bin symlink puts it on
# PATH.
install -m 0755 build/linux/cli/labrador-cli "$PKG/usr/lib/labrador/labrador-cli"
cp -r assets/. "$PKG/usr/lib/labrador/assets/"
mkdir -p "$PKG/usr/lib/labrador/assets/firmware"
cp firmware/*.hex "$PKG/usr/lib/labrador/assets/firmware/"
ln -s ../lib/labrador/labrador "$PKG/usr/bin/labrador"
ln -s ../lib/labrador/labrador-cli "$PKG/usr/bin/labrador-cli"
cp packaging/69-labrador.rules "$PKG/lib/udev/rules.d/"
cp packaging/espotek-labrador.desktop "$PKG/usr/share/applications/"
cp packaging/icon256.png "$PKG/usr/share/icons/hicolor/256x256/apps/espotek-labrador.png"

INSTALLED_KB=$(du -sk "$PKG/usr" | cut -f1)
cat > "$PKG/DEBIAN/control" <<EOF
Package: espotek-labrador
Version: ${VERSION}
Section: electronics
Priority: optional
Architecture: ${DEB_ARCH}
Maintainer: Chris Esposito <admin@espotek.com>
Installed-Size: ${INSTALLED_KB}
Depends: libc6, libstdc++6, libusb-1.0-0, libgl1, libopengl0, libx11-6
Homepage: https://espotek.com
Description: driver and GUI for EspoTek Labrador
 The EspoTek Labrador is an open-source board that turns your PC, Raspberry
 Pi, or Android smartphone into a full-featured electronics lab bench,
 complete with oscilloscope, signal generator, and more.
 .
 This package contains the SDL3/ImGui unified GUI, the headless
 labrador-cli tool (triggered captures to CSV, firmware updates) and the
 firmware images both flash to the board.
EOF

cat > "$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=usb || true
fi
EOF
chmod 0755 "$PKG/DEBIAN/postinst"

dpkg-deb --root-owner-group --build "$PKG" "$DEB"
dpkg-deb --info "$DEB"
test "$(dpkg-deb --field "$DEB" Architecture)" = "$DEB_ARCH"
echo "::endgroup::"

# Install the package into the build container (root, no udev, so the
# postinst's udevadm calls are skipped) and run the CLI from PATH.
# update-firmware with no board must get as far as looking for one, which
# proves /usr/bin/labrador-cli -> /usr/lib/labrador resolves the bundled hex.
echo "::group::Smoke test labrador-cli from the installed .deb"
dpkg -i "$DEB"
check_elf /usr/lib/labrador/labrador /usr/lib/labrador/labrador-cli
command -v labrador-cli
labrador-cli --help
labrador-cli help capture
labrador-cli help update-firmware
if labrador-cli update-firmware >cli.log 2>&1; then
    echo "ERROR: update-firmware succeeded with no board attached" >&2
    exit 1
fi
cat cli.log
grep -q "no Labrador board found" cli.log
# The package must carry the board's (variant 3) firmware that the tree has, whatever its
# version, rather than a file name that goes stale when the firmware is bumped.
FW=$(cd firmware && ls labrafirm_*_03.hex)
test "$(echo "$FW" | wc -l)" -eq 1
test -f "/usr/lib/labrador/assets/firmware/$FW"
echo "::endgroup::"
