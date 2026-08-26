/* Real Apple crt_externs.c provides _NSGetArgc() as `return &NXArgc;`
 * (third_party/Libc/sys/crt_externs.c) -- not reused verbatim here
 * because that same file also defines _NSGetArgv/_NSGetEnviron/
 * _NSGetProgname/_NSGetMachExecuteHeader, which already have their own
 * real, separate implementations elsewhere in this project's curated
 * stack (cf_libc_extras.a's own __NSGetArgv, etc) -- compiling the
 * whole file would be a real duplicate-symbol conflict. This project's
 * own third_party/Csu/crt.c already defines and maintains the real
 * NXArgc global at process startup; this is the exact same one-line
 * accessor real Apple's own crt_externs.c provides for it, just
 * extracted standalone to avoid that conflict.
 */
extern int NXArgc;
int *_NSGetArgc(void) {
	return &NXArgc;
}
