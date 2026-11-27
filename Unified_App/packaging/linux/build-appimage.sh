#!/usr/bin/env bash
# Builds Unified_App/Labrador-for-Linux-<arch>.AppImage: the GUI and the
# headless labrador-cli, the firmware hex, udev rules, icon and .desktop
# entry, packed by linuxdeploy.  The binaries statically link libstdc++/libgcc
# (so the host's C++ runtime version is irrelevant) and libusb, and are
# checked for a glibc 2.31 floor; the finished AppImage is smoke-tested
# through labrador-cli.  Runs as root in a container prepared by
# install-build-deps.sh appimage, on the architecture it builds for.
#
#   build-appimage.sh x86_64|arm64|i386 <version>
set -euxo pipefail

ARCH=$1
VERSION=$2
cd "$(dirname "$(readlink -f "$0")")/../.."
. packaging/linux/arch.sh

# No FUSE in the container: linuxdeploy and the finished AppImage extract
# themselves instead of mounting.
export APPIMAGE_EXTRACT_AND_RUN=1

# CMake embeds the commit in the app (read quietly, so a git that refuses
# the checkout would leave it empty) and linuxdeploy names the AppImage
# after it, so git must work here.
git rev-parse HEAD

echo "::group::Build static libusb"
# Static libusb for the self-contained AppImage binary. --disable-udev:
# hotplug via netlink and sysfs enumeration, no libudev dependency.
wget -nv https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.tar.bz2
tar xjf libusb-1.0.29.tar.bz2
(
    cd libusb-1.0.29
    ./configure --prefix="$PWD/../libusb-static" --disable-udev \
        --enable-static --disable-shared
    make -j"$(nproc)"
    make install
)
echo "::endgroup::"

echo "::group::Build"
export PKG_CONFIG_PATH="$PWD/libusb-static/lib/pkgconfig"
cmake --preset appimage -DLABRADOR_VERSION="$VERSION"
cmake --build --preset appimage
echo "::endgroup::"

echo "::group::Test libdfuprog"
# Unit test for the in-app flasher's command splitting (#450); links the
# static libusb built above.
make -C ../libdfuprog/tests test
echo "::endgroup::"

echo "::group::Verify portability"
# The GUI and labrador-cli ship in the same AppImage, so both must meet the
# floors.
for BIN in build/appimage/labrador build/appimage/cli/labrador-cli; do
    check_elf "$BIN"
    # glibc floor must stay at the container's 2.31.
    objdump -p "$BIN" | grep NEEDED
    MAX=$(objdump -T "$BIN" | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -Vu | tail -1)
    echo "$BIN max glibc symbol: $MAX"
    test "$(printf '%s\nGLIBC_2.31\n' "$MAX" | sort -V | tail -1)" = "GLIBC_2.31"
    # C++ runtime and libusb are baked in, not dynamic dependencies.
    if objdump -p "$BIN" | grep -E 'NEEDED.*(libstdc\+\+|libgcc_s|libusb)'; then
        echo "ERROR: unexpected dynamic dependency in $BIN" >&2
        exit 1
    fi
done
# Both video backends (and libdecor CSD support) must be compiled in.
CFG=$(find build/appimage -name SDL_build_config.h | head -1)
grep -q "#define SDL_VIDEO_DRIVER_WAYLAND 1" "$CFG"
grep -q "#define SDL_VIDEO_DRIVER_X11 1" "$CFG"
grep -q "#define HAVE_LIBDECOR_H 1" "$CFG"
echo "::endgroup::"

echo "::group::Assemble AppDir"
APPDIR=AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin/assets" \
         "$APPDIR/usr/lib/udev/rules.d" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/apprun-hooks"
cp build/appimage/labrador "$APPDIR/usr/bin/labrador"
# labrador-cli sits beside the GUI; the zz-labrador-cli.sh AppRun hook
# routes `<AppImage> cli ...` (or an ARGV0 of labrador-cli) to it.
cp build/appimage/cli/labrador-cli "$APPDIR/usr/bin/labrador-cli"
# SDL_GetBasePath() resolves /proc/self/exe -> usr/bin, so assets live
# there, shared by both binaries (the CLI's update-firmware flashes the same
# hex the GUI does).
cp -r assets/. "$APPDIR/usr/bin/assets/"
mkdir -p "$APPDIR/usr/bin/assets/firmware"
cp firmware/*.hex "$APPDIR/usr/bin/assets/firmware/"
cp packaging/69-labrador.rules "$APPDIR/usr/lib/udev/rules.d/"
cp packaging/espotek-labrador.desktop "$APPDIR/usr/share/applications/"
cp packaging/icon256.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/espotek-labrador.png"
cp packaging/linux/apprun-hooks/*.sh "$APPDIR/apprun-hooks/"
chmod +x "$APPDIR/apprun-hooks/"*.sh
echo "::endgroup::"

echo "::group::Build AppImage"
LD=linuxdeploy-$LINUXDEPLOY_ARCH.AppImage
wget -N -nv "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/${LD}"
chmod +x "$LD"
export LINUXDEPLOY_OUTPUT_APP_NAME=Labrador
LINUXDEPLOY_OUTPUT_VERSION=$(git rev-parse --short HEAD)
export LINUXDEPLOY_OUTPUT_VERSION
"./$LD" --appdir=AppDir \
    --executable=AppDir/usr/bin/labrador \
    --desktop-file=AppDir/usr/share/applications/espotek-labrador.desktop \
    --icon-file=AppDir/usr/share/icons/hicolor/256x256/apps/espotek-labrador.png \
    --output=appimage
mv Labrador*.AppImage "Labrador-for-Linux-$ARCH.AppImage"
echo "::endgroup::"

# The CLI needs no display, so it can exercise the finished AppImage here.
# update-firmware with no board must get as far as looking for one, which
# proves it found the bundled hex under usr/bin/assets.
echo "::group::Smoke test labrador-cli in the AppImage"
IMG="./Labrador-for-Linux-$ARCH.AppImage"
# The AppImage runtime and the binaries packed inside it are all $ARCH's.
check_elf "$IMG"
EXTRACTED=$(mktemp -d)
(cd "$EXTRACTED" && "$OLDPWD/$IMG" --appimage-extract >/dev/null)
check_elf "$EXTRACTED/squashfs-root/usr/bin/labrador" \
          "$EXTRACTED/squashfs-root/usr/bin/labrador-cli"
"$IMG" cli --help
"$IMG" cli help capture
"$IMG" cli help update-firmware
if "$IMG" cli update-firmware >cli.log 2>&1; then
    echo "ERROR: update-firmware succeeded with no board attached" >&2
    exit 1
fi
cat cli.log
grep -q "no Labrador board found" cli.log
# Invoked through a symlink named labrador-cli the AppImage runtime sets
# ARGV0 to that name and the hook routes to the CLI as well.
ln -sf "$IMG" labrador-cli
./labrador-cli --help | grep -q "command-line tools for the EspoTek Labrador"
echo "::endgroup::"
