# Vendored libusb (Windows builds)

This is **libusb 1.0.30**, the Windows subset of its sources, with one patch
applied. `cmake/BundledLibusb.cmake` compiles it into the Windows exe as the
static target `labrador_libusb`; macOS, Linux, Pi and Android builds don't
use it (they take libusb from the system or `deps/libusb-android`).

## Why a copy, and why patched

In-app firmware updates on Windows go through the Atmel DFU bootloader
(`03EB:2FE4`), bound to the **libusb-win32** kernel driver (`libusb0.sys`),
which libusb-1.0 drives through `libusbK.dll`. With the libusbK.dll that the
driver package installs (3.0.7.0), every control transfer to that device is
completed by the driver *before* `DeviceIoControl()` returns, and libusbK
hands libusb's OVERLAPPED straight to the kernel without reporting the byte
count back. Released libusb then overwrites the byte count the kernel had
already stored with an uninitialised value and posts a second completion.
dfu-programmer sees successful transfers come back with `actual_length` 0
(or garbage), aborts the flash after the erase, and the board is left stuck
in the bootloader (espotek-org/Labrador#450, #458 for the measurements, #459
for the diagnosis).

No libusb release has the fix yet, so the sources live here with it applied.
Keeping a copy in the repo (rather than fetching and patching a tarball at
configure time) means the build is reproducible offline and the exact code
shipped is reviewable in git.

## Provenance

| | |
|---|---|
| Release | libusb 1.0.30 |
| Tarball | `https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2` |
| SHA-256 | `fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf` |
| Licence | LGPL-2.1-or-later (`COPYING`) |

Files copied verbatim from the tarball (nothing else is needed to build the
Windows backend; `update.sh` holds the same list):

```
libusb/{core,descriptor,hotplug,io,strerror,sync}.c
libusb/{libusb,libusbi,version,version_nano}.h
libusb/os/{events_windows,threads_windows,windows_common,windows_usbdk,windows_winusb}.{c,h}
msvc/config.h          # libusb's hand-written config.h, used for MSVC builds
COPYING
```

MinGW builds use `../../cmake/libusb_mingw_config.h` as `config.h` instead
(the equivalent of what libusb's `./configure` generates for a MinGW static
build).

## Patches (`patches/`)

Applied on top of the copied files, in order. Each one is the same change as
submitted upstream, so it can be dropped as soon as a libusb release carries
it.

1. `0001-windows-do-not-synthesize-a-completion-the-kernel-already-delivered.patch`
   — `libusb/os/windows_common.c`: `windows_submit_transfer()` marks the
   OVERLAPPED `STATUS_PENDING` before the backend runs, and
   `windows_force_sync_completion()` returns early when the kernel has
   already replaced that status, because the kernel's completion packet
   (with the right byte count) is then already on its way through the
   completion port. Upstream: libusb fork branch
   `EspoTek/libusb:windows-sync-control-length` (PR to follow once verified
   on hardware). Supersedes the earlier "prefer the completion-port length
   when the two disagree" variant from #459, which guarded against the
   symptom; this removes the double completion that caused it.

## Updating

```sh
Unified_App/deps/libusb/update.sh 1.0.31 <sha256 of the release tarball>
```

The script downloads the release tarball, verifies the hash, replaces the
copied files, and re-applies every patch in `patches/`. If a patch no longer
applies, check whether upstream shipped the fix (the `windows_common.c`
hunks will be there already) and delete the patch; otherwise rebase it. Then
update the table above and the version in this file's title.
