# Sourced (not run) by bind_target_audit.sh and weak_def_audit.sh, from
# tools/userland_staging/.
#
# staged_images [DYLIB|EXECUTE|BUNDLE]: print every (or only that kind of)
# Mach-O executable, dylib and bundle in the
# staged install trees that inject_into_sd_image.sh ditto's onto the SD
# image as whole directories (libc_build/x11/server/root,
# libc_build/gnustep/root, libc_build/fontstack/{root,bin}, ...), one path
# per line.
#
# Why: both audits used to sweep only libc_build/system/*.dylib
# and the top-level *.macho files. DAR-447 removed atan2 (and sin/cos/
# ceil/...) from libsystem_c.dylib, relinked the images it knew about and
# reported a clean no-arg sweep -- but /usr/X11/bin/Xorg lives under
# libc_build/x11/server/root, was never swept, still bound
# libsystem_c/_atan2, and aborted at dyld load on a real Pi 3. Any image the
# SD image ships has to be in the sweep, or "clean" means nothing for it.
#
# Symlinks are skipped (find -type f) so a versioned dylib is audited once.
# Object files, host tools (non-arm64, or arm64 with LC_BUILD_VERSION
# platform 1 = macOS) and anything that is not 64-bit Mach-O are skipped.
staged_images() {
    local f kind="${1:-EXECUTE|DYLIB|BUNDLE}"
    find libc_build/*/root libc_build/*/*/root libc_build/*/bin -type f 2>/dev/null |
        sort |
        while IFS= read -r f; do
            [ "$(od -An -N4 -tx1 "$f" 2>/dev/null | tr -d ' \n')" = "cffaedfe" ] || continue
            otool -hv "$f" 2>/dev/null | grep -qE "ARM64 .* ($kind) " || continue
            otool -l "$f" 2>/dev/null | grep -q '^ *platform 1$' && continue
            printf '%s\n' "$f"
        done
}
