#!/bin/sh
# Applies this port's Window Maker overrides to a generated global WindowMaker defaults file (the stock file
# WindowMaker/Defaults/WindowMaker.in after build_wmaker.sh's sed). Naples (dock, menu bar, minimized windows)
# replaces Window Maker's own dock, clip and workspace UI: no dock, no clip, no drawers, no workspace pager or name
# popup, and no miniwindow icons (Naples lists the minimized windows instead). Keys are replaced in place; a key
# missing from the file is an error rather than a silent no-op. Idempotent.
# usage: apply_defaults.sh <WindowMaker defaults file>
f="$1"
[ -f "$f" ] || { echo "error: $f not found" >&2; exit 1; }
for kv in DisableDock=YES DisableClip=YES DisableDrawers=YES DisableMiniwindows=YES EnableWorkspacePager=NO \
          WorkspaceNameDisplayPosition=none; do
    k=${kv%%=*}; v=${kv#*=}
    grep -q "^[[:space:]]*$k = " "$f" || { echo "error: $k not in $f" >&2; exit 1; }
    sed -i.bak -e "s:^\\([[:space:]]*$k = \\)[^;]*;:\\1$v;:" "$f"
done
rm -f "$f.bak"
