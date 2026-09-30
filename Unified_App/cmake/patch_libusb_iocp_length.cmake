# Applied to the libusb 1.0.30 release sources by BundledLibusb.cmake
# (cmake -DLIBUSB_SOURCE_DIR=<dir> -P patch_libusb_iocp_length.cmake).
#
# Bug: with the libusb-win32 driver (libusb0.sys, reached through libusbK.dll)
# the Windows backend's I/O completion port reports the right byte count, but
# the later GetOverlappedResult() call in windows_handle_transfer_completion()
# often returns 0 (sometimes garbage) for the same transfer.  libusb uses the
# latter, so successful control transfers come back with actual_length 0 and
# dfu-programmer aborts firmware flashes halfway, leaving boards stuck in the
# DFU bootloader (espotek-org/Labrador#450).  Fix: remember the IOCP count and
# prefer it when the two disagree.  For every other driver the two counts are
# identical, so behaviour there is unchanged.

set(_marker "LABRADOR_PATCH_IOCP_LENGTH")

# _labrador_patch_file(<file> <old1> <new1> [<old2> <new2> ...])
function(_labrador_patch_file path)
    file(READ "${path}" _text)
    if(_text MATCHES "${_marker}")
        return() # already patched (re-configure of an existing checkout)
    endif()
    # The release tarball is LF; normalise in case the tree came from a CRLF
    # git checkout (FETCHCONTENT_SOURCE_DIR_LABRADOR_LIBUSB).
    string(REPLACE "\r\n" "\n" _text "${_text}")
    math(EXPR _last "${ARGC} - 1")
    foreach(_i RANGE 1 ${_last} 2)
        math(EXPR _j "${_i} + 1")
        set(_old "${ARGV${_i}}")
        set(_new "${ARGV${_j}}")
        string(FIND "${_text}" "${_old}" _pos)
        if(_pos EQUAL -1)
            message(FATAL_ERROR "libusb patch: anchor not found in ${path}:\n${_old}")
        endif()
        string(REPLACE "${_old}" "${_new}" _text "${_text}")
    endforeach()
    file(WRITE "${path}" "${_text}")
endfunction()

if(NOT LIBUSB_SOURCE_DIR)
    message(FATAL_ERROR "LIBUSB_SOURCE_DIR not set")
endif()

_labrador_patch_file("${LIBUSB_SOURCE_DIR}/libusb/os/windows_common.h"
"struct windows_transfer_priv {
\tOVERLAPPED overlapped;
\tHANDLE handle;
"
"struct windows_transfer_priv {
\tOVERLAPPED overlapped;
\tHANDLE handle;
\tDWORD iocp_bytes; /* ${_marker}: byte count reported by the completion port */
")

_labrador_patch_file("${LIBUSB_SOURCE_DIR}/libusb/os/windows_common.c"
"\t\tusbi_signal_transfer_completion(itransfer);
"
"\t\ttransfer_priv->iocp_bytes = num_bytes; /* ${_marker} */
\t\tusbi_signal_transfer_completion(itransfer);
"
"\telse
\t\tresult = GetLastError();
"
"\telse
\t\tresult = GetLastError();

\t/* ${_marker}: with libusb0.sys via libusbK.dll, GetOverlappedResult() can
\t * report a different (usually 0) length than the completion port did. */
\tif (result == NO_ERROR && bytes_transferred != transfer_priv->iocp_bytes) {
\t\tusbi_dbg(ctx, \"overlapped length %lu differs from IOCP length %lu; using IOCP\",
\t\t\t ULONG_CAST(bytes_transferred), ULONG_CAST(transfer_priv->iocp_bytes));
\t\tbytes_transferred = transfer_priv->iocp_bytes;
\t}
")
