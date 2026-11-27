#!/usr/bin/env bash
# Installs what the Linux package builds need, as root in a fresh container:
# the distro's build packages, cmake/ninja/meson from PyPI and libdecor built
# from source.  The x86_64 and arm64 packages build on Ubuntu 20.04 and i386
# on Debian 11 (Ubuntu 20.04 has no i386 release); both have glibc 2.31,
# which is the packages' glibc floor.
#
#   install-build-deps.sh appimage|deb
set -euxo pipefail

KIND=$1
export DEBIAN_FRONTEND=noninteractive

PACKAGES=(build-essential git ca-certificates wget file pkg-config python3-pip
    libgl1-mesa-dev libegl1-mesa-dev
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev
    libxi-dev libxss-dev libxtst-dev libxkbcommon-dev
    libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev libdbus-1-dev)
case "$KIND" in
    # The AppImage links a static libusb built from its .tar.bz2 release.
    appimage) PACKAGES+=(bzip2) ;;
    # The .deb links the distro's shared libusb.
    deb) PACKAGES+=(libusb-1.0-0-dev) ;;
    *)
        echo "usage: $0 appimage|deb" >&2
        exit 1
        ;;
esac

apt-get -o Acquire::Retries=3 update
# ports.ubuntu.com has partial outages; retry the whole install
# (fetched .debs stay cached, retries only pull the stragglers).
ok=""
for i in 1 2 3; do
    if apt-get -o Acquire::Retries=3 install -y "${PACKAGES[@]}"; then
        ok=1
        break
    fi
    echo "apt-get install failed (attempt $i); retrying"
    sleep 20
    apt-get -o Acquire::Retries=3 update || true
done
test -n "$ok"

# The distros' cmake is too old for the build (20.04 has 3.16, Debian 11
# 3.18), as is 20.04's meson (0.53); ninja to match.  PyPI has cmake and
# ninja wheels for x86_64, aarch64 and i686, and pip picks the newest meson
# that supports the distro's python3.
pip3 install "cmake>=3.22,<4" ninja meson

# libdecor isn't packaged on Ubuntu 20.04, and every arch builds the same
# release of it.  SDL compiles Wayland CSD support against these headers and
# dlopens libdecor-0.so.0 from the host at runtime, so the build products
# themselves never link it.
WORK=$(mktemp -d)
cd "$WORK"
wget -nv https://gitlab.freedesktop.org/libdecor/libdecor/-/archive/0.2.2/libdecor-0.2.2.tar.gz
tar xzf libdecor-0.2.2.tar.gz
cd libdecor-0.2.2
meson setup build --prefix=/usr --buildtype=release -Ddemo=false -Dgtk=disabled
ninja -C build install
