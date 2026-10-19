#!/bin/sh
# AppImage AppRun hook: run the headless labrador-cli instead of the GUI when
# the AppImage is started as
#
#   ./Labrador-for-Linux-x86_64.AppImage cli <command> [options]
#
# or through a symlink (or copy) named labrador-cli, e.g.
#
#   ln -s ~/Labrador-for-Linux-x86_64.AppImage ~/.local/bin/labrador-cli
#
# (the AppImage runtime passes the name it was invoked by in ARGV0).  Hooks
# are sourced in name order, so the zz- prefix keeps this after the udev
# hook: CLI users get the udev rules offered too.  exec replaces AppRun, so
# the GUI never starts.

if [ "$1" = "cli" ]; then
    shift
    exec "$this_dir/usr/bin/labrador-cli" "$@"
fi

case "$(basename "${ARGV0:-}")" in
    labrador-cli|labrador-cli.AppImage)
        exec "$this_dir/usr/bin/labrador-cli" "$@"
        ;;
esac
