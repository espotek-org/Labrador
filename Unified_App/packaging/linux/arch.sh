# Sourced by build-appimage.sh and build-deb.sh with ARCH set to the name in
# the release files (Labrador-for-Linux-<ARCH>.AppImage / .deb).  Sets the
# names other tools use for that architecture, and checks that this machine
# is it, so an i386 build missing docker's --platform linux/386 or linux32
# (which makes uname -m report i686) stops here instead of building the
# wrong thing.

case "$ARCH" in
    x86_64)
        MACHINE=x86_64               # uname -m
        DEB_ARCH=amd64               # dpkg --print-architecture
        LINUXDEPLOY_ARCH=x86_64      # linuxdeploy-<arch>.AppImage
        ELF_DESC='ELF 64-bit LSB .*, x86-64,'
        ;;
    arm64)
        MACHINE=aarch64
        DEB_ARCH=arm64
        LINUXDEPLOY_ARCH=aarch64
        ELF_DESC='ELF 64-bit LSB .*, ARM aarch64,'
        ;;
    i386)
        MACHINE=i686
        DEB_ARCH=i386
        LINUXDEPLOY_ARCH=i386
        ELF_DESC='ELF 32-bit LSB .*, Intel 80386,'
        ;;
    *)
        echo "unknown arch '$ARCH' (expected x86_64, arm64 or i386)" >&2
        exit 1
        ;;
esac
test "$(uname -m)" = "$MACHINE"
test "$(dpkg --print-architecture)" = "$DEB_ARCH"

# check_elf FILE...: fails unless file(1) reports every FILE as an ELF
# binary for $ARCH.
check_elf() {
    local f
    for f in "$@"; do
        file -L "$f"
        file -L "$f" | grep -qE "$ELF_DESC"
    done
}
