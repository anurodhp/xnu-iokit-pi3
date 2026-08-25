/* Real global (see libc_locale_env_stubs.c's own comment): real Apple
 * Csu/crt.c's _start() sets this at process startup; every static binary
 * in this project gets it that way already. libSystem.B.dylib has no
 * crt0 of its own, so this is the real, correct place for the actual
 * STORAGE to live (matching real Apple: __progname's storage lives in
 * Libc/libSystem, crt0 just assigns into it) -- zero-initialized
 * default, same as a fresh process before argv[0] is known.
 *
 * NOT needed by (and must NOT be linked into) libdyld.dylib -- real
 * libdyldGlue.o already defines this itself (dyldMain.cpp's real
 * state.vars->__prognamePtr wiring), a genuine duplicate-symbol
 * conflict there. */
const char *__progname = 0;
