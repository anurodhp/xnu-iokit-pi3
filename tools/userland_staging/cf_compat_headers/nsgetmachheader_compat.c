/* Real fix, DAR-253 (configd SIGSEGV parsing its first real Info.plist
 * bundle -- see that Linear ticket for the full repro).
 *
 * swift-corelibs-foundation's CFBundle_Grok.c
 * (_CFBundleCreateInfoDictFromMainExecutable(), called from
 * CFBundle.c's __CFBundleCreateInfoDictFromMainExecutable() ->
 * __CFBundleInitializeMainBundleInfoDictionaryAlreadyLocked() ->
 * __CFBundleGetMainBundleAlreadyLocked() -> CFBundleGetMainBundle(),
 * itself reached from plain CFBundleCreate() the first time any bundle
 * is created in a process) calls the real Apple crt_externs.h API
 * _NSGetMachExecuteHeader() to get the CURRENT PROCESS's own main
 * executable Mach header, then hands it to cctools'
 * getsegbyname() (third_party/cctools/libmacho/getsegbyname.c,
 * unmodified real Apple source) to walk its load commands looking for
 * "__TEXT" so it can find an embedded __info_plist section.
 *
 * This project's ONLY other definition of _NSGetMachExecuteHeader()
 * anywhere in the userland stack is libmalloc_dispatch.c's, which
 * unconditionally `return (struct mach_header *)0` -- a real, correct
 * answer for this project's fully-static PID-1-style binaries (no
 * dyld, no ASLR ever applied, see that file's own header comment). But
 * it is also the ONLY dylib anywhere in libc_build/system/ that exports
 * this symbol (confirmed via `dyld_info -exports` on every dylib in
 * that directory), so every dynamically-linked consumer -- CF included
 * -- was binding to that NULL stub (confirmed via
 * `dyld_info -fixups libCoreFoundation.dylib`, which showed
 * "bind libsystem_malloc/__NSGetMachExecuteHeader").
 *
 * Real, confirmed crash (real QEMU `-user_fault_debug` capture,
 * qemu/dar253_boot7.log): cctools' getsegbyname() LP64 body does
 * `mhp = _NSGetMachExecuteHeader();` then unconditionally reads
 * `mhp->ncmds` (offset 0x10 in struct mach_header_64) with no NULL
 * check -- a real NULL+0x10 dereference, matching this exact crash's
 * real `far 0x10` fault address exactly. Real backtrace (symbolicated
 * against this dylib's own `nm -n`, matches exactly):
 *   CFBundleCreateBundlesFromDirectory -> CFBundleCreate ->
 *   CFBundleGetMainBundle -> __CFBundleGetMainBundleAlreadyLocked ->
 *   __CFBundleInitializeMainBundleInfoDictionaryAlreadyLocked ->
 *   __CFBundleCreateInfoDictFromMainExecutable -> getsegbyname -> crash.
 * Not caused by anything specific to InterfaceNamer's own Info.plist
 * content (no XML/DOCTYPE parsing had even started yet) -- this crash
 * fires the first time ANY bundle is created in a CF-linked process
 * that has never previously created one, which is exactly
 * CFBundleCreateBundlesFromDirectory()'s very first call in
 * plugin_exec() (third_party/configd/configd.tproj/plugin_support.c).
 *
 * Real fix: for a dynamically-linked process under this project's real
 * dyld-940 port, the main executable's Mach header is simply
 * `_dyld_get_image_header(0)` -- image index 0 is always the main
 * executable (real, unmodified dyld convention). This project's
 * libdyld.dylib already provides a real, unmodified implementation
 * (libdyld/libdyldGlue.cpp's `_dyld_get_image_header()` forwards
 * straight to dyld's own real API table,
 * `gDyld.apis->_dyld_get_image_header()`) -- the SAME real dyld
 * mechanism CFUniChar.c's own __CFGetSectDataPtr() and this same
 * CFBundle_Grok.c's own _CFBundleGetSectData() already call
 * successfully elsewhere in this exact dylib, so this is a proven,
 * already-working real API in this exact binary, not an unverified
 * path. On real macOS this value instead flows through dyld's
 * ProgramVars struct -> libSystem_initializer() -> _libc_initializer()
 * -> _program_vars_init() -> crt_externs.c's static
 * `_mh_execute_header_pointer` -- this project's simplified
 * libSystem.B.dylib never runs that real chain at all (see
 * libsystem_dyld_initializer_compat.c's own header for the documented,
 * deliberate gap), so porting the entire ProgramVars/_libc_initializer
 * plumbing just for this one accessor is out of scope for this fix;
 * `_dyld_get_image_header(0)` is an equally-real, official dyld public
 * API (<mach-o/dyld.h>) returning the identical real pointer value
 * ProgramVars.mh would have carried, not a fabricated stand-in.
 *
 * Deliberately scoped to libCoreFoundation.dylib's own link only (a
 * new local, compiled-in definition here, NOT a change to
 * libmalloc_dispatch.c/libsystem_malloc.dylib itself): that dylib is a
 * shared sibling of every dynamically- AND statically-linked consumer
 * in this project, so changing its own copy would risk exactly the
 * "duplicate-static-copy"/weak-def-audit hazard class this project has
 * hit before, for a symbol with genuinely different correct answers in
 * the static-PID-1 vs real-dyld contexts. Providing CF's own local,
 * strong definition here means CF's own reference to
 * _NSGetMachExecuteHeader resolves locally at link time (no
 * two-level-namespace ambiguity against libsystem_malloc.dylib's
 * unrelated export -- confirmed via a post-link `dyld_info -fixups`
 * check that the symbol no longer appears as an external bind at all),
 * while every OTHER consumer of libsystem_malloc.dylib keeps its
 * existing, correct-for-them NULL stub untouched. */
struct mach_header;
extern const struct mach_header *_dyld_get_image_header(unsigned int image_index);

struct mach_header *_NSGetMachExecuteHeader(void) {
    return (struct mach_header *)_dyld_get_image_header(0);
}
