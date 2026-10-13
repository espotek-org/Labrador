#!/usr/bin/env bash
# Replace the vendored libusb sources with a given release and re-apply the
# patches in patches/.  See README.md.
#
#   update.sh <version> <sha256>
#   e.g. update.sh 1.0.30 fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <libusb version> <sha256 of libusb-<version>.tar.bz2>" >&2
    exit 2
fi
version=$1
sha256=$2

here=$(cd "$(dirname "$0")" && pwd)
url="https://github.com/libusb/libusb/releases/download/v${version}/libusb-${version}.tar.bz2"

# Exactly what cmake/BundledLibusb.cmake compiles, plus the headers it includes.
files=(
    libusb/core.c libusb/descriptor.c libusb/hotplug.c libusb/io.c
    libusb/strerror.c libusb/sync.c
    libusb/libusb.h libusb/libusbi.h libusb/version.h libusb/version_nano.h
    libusb/os/events_windows.c libusb/os/events_windows.h
    libusb/os/threads_windows.c libusb/os/threads_windows.h
    libusb/os/windows_common.c libusb/os/windows_common.h
    libusb/os/windows_usbdk.c libusb/os/windows_usbdk.h
    libusb/os/windows_winusb.c libusb/os/windows_winusb.h
    msvc/config.h
    COPYING
)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "Downloading $url"
curl -fsSL -o "$tmp/libusb.tar.bz2" "$url"
echo "${sha256}  $tmp/libusb.tar.bz2" | shasum -a 256 -c -
tar xjf "$tmp/libusb.tar.bz2" -C "$tmp"
src="$tmp/libusb-${version}"

echo "Replacing sources"
rm -rf "$here/libusb" "$here/msvc" "$here/COPYING"
for f in "${files[@]}"; do
    mkdir -p "$here/$(dirname "$f")"
    cp "$src/$f" "$here/$f"
done

for p in "$here"/patches/*.patch; do
    echo "Applying $(basename "$p")"
    patch -p1 -d "$here" < "$p"
done

echo "Done: libusb $version. Update the provenance table in README.md."
