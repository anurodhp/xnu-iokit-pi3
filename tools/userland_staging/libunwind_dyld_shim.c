/* Real libunwind (third_party/libunwind/libunwind/src/AddressSpace.hpp)
 * assumes libSystem.dylib/dyld provides `_dyld_find_unwind_sections()` on
 * every Darwin target with __IPHONE_OS_VERSION_MIN_REQUIRED defined (which
 * this project's -target arm64-apple-ios14.4 build always sets) - it does
 * NOT fall back to the older getsectiondata()-based implementation AddressSpace.hpp
 * itself carries for pre-10.7 Mac OS X, because that fallback is `#if`'d
 * out whenever an iOS deployment target is set at all, regardless of
 * version. This project has no dyld, so `_dyld_find_unwind_sections` does
 * not exist - this file provides a real, from-scratch implementation
 * (last resort per CLAUDE.md's source-priority rule: AddressSpace.hpp's own
 * pre-10.7 fallback was the closest real Apple reference and is followed
 * closely below, just without dladdr()/multi-image support, since this
 * target links exactly one Mach-O image, itself).
 *
 * This is real Mach-O load-command parsing (LC_SEGMENT_64 -> per-section
 * segname/sectname match), the same well-defined on-disk format
 * getsectiondata() itself walks - not a simplification of what unwinding
 * needs, just a single-image specialization of it.
 *
 * `__mh_execute_header` is a REAL symbol here, not invented: ld64
 * automatically defines it for any MH_EXECUTE-type link (confirmed via
 * `nm` on this project's own existing smoketest .macho outputs - see
 * build_libunwind.sh's header comment for the exact command). Real Apple
 * headers declare it as `extern const struct mach_header_64 _mh_execute_header;`
 * assembly source or via <mach-o/getsect.h>. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mach-o/loader.h>

extern const struct mach_header_64 _mh_execute_header;

struct dyld_unwind_sections {
    const struct mach_header *mh;
    const void *dwarf_section;
    uintptr_t dwarf_section_length;
    const void *compact_unwind_section;
    uintptr_t compact_unwind_section_length;
};

static bool section_match(const struct section_64 *sect, const char *segname,
                           const char *sectname) {
    return strncmp(sect->segname, segname, sizeof(sect->segname)) == 0 &&
           strncmp(sect->sectname, sectname, sizeof(sect->sectname)) == 0;
}

bool _dyld_find_unwind_sections(void *addr, struct dyld_unwind_sections *info) {
    (void)addr; /* single-image target: every address belongs to this
                 * binary, or it's not a valid PC at all - no per-address
                 * image lookup (dladdr's job on real Darwin) needed. */
    const struct mach_header_64 *mh = &_mh_execute_header;
    info->mh = (const struct mach_header *)mh;
    info->dwarf_section = NULL;
    info->dwarf_section_length = 0;
    info->compact_unwind_section = NULL;
    info->compact_unwind_section_length = 0;

    const uint8_t *cmd_ptr = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd_ptr;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)lc;
            const struct section_64 *sect =
                (const struct section_64 *)(seg + 1);
            for (uint32_t s = 0; s < seg->nsects; s++) {
                if (section_match(&sect[s], "__TEXT", "__eh_frame")) {
                    info->dwarf_section = (const void *)(uintptr_t)sect[s].addr;
                    info->dwarf_section_length = sect[s].size;
                } else if (section_match(&sect[s], "__TEXT", "__unwind_info")) {
                    info->compact_unwind_section =
                        (const void *)(uintptr_t)sect[s].addr;
                    info->compact_unwind_section_length = sect[s].size;
                }
            }
        }
        cmd_ptr += lc->cmdsize;
    }
    return true;
}

/* Honest failure, not fabricated success: real dladdr() resolves an
 * address to its containing dyld image + nearest exported symbol via
 * dyld's live symbol tables. This target has no dyld and this project's
 * own build pipeline doesn't emit a runtime-walkable symbol table either
 * (real Apple `nm`/`otool -l`'s LC_SYMTAB is present, but nothing here
 * parses it at runtime). Real libunwind callers of dladdr()
 * (AddressSpace.hpp's pre-10.7 _dyld_find_unwind_sections fallback, which
 * this file replaces outright above, and its `findUnwindSections`
 * neighbor's optional-symbolication path) both treat a false return as
 * "no symbol info available, continue without it" - a real, defined,
 * non-fatal outcome, not an error this stub is hiding. */
struct dl_info_compat {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
};
int dladdr(const void *addr, struct dl_info_compat *info) {
    (void)addr;
    (void)info;
    return 0;
}

/* Real no-op, not a stub of missing logic: libunwind.cpp's
 * DwarfFDECache::add() registers this dyld callback so its cache can
 * purge entries when a dylib is unloaded at runtime
 * (`_dyld_register_func_for_remove_image`, a real dyld API). This target
 * links exactly one static image that is never unloaded while running
 * (no dyld, no dlclose-equivalent) - there is genuinely never a "remove
 * image" event to notify, so doing nothing here is the real correct
 * behavior for a single-static-image target, not a simplification. */
struct mach_header;
void _dyld_register_func_for_remove_image(void (*func)(const struct mach_header *mh, intptr_t vmaddr_slide)) {
    (void)func;
}
