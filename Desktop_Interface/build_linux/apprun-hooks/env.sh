#!/bin/sh

if [ "x$this_dir" = "x" ]; then
    this_dir="$(readlink -f "$(dirname "$0")")"
fi

XDG_DATA_DIRS="$this_dir/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export XDG_DATA_DIRS
