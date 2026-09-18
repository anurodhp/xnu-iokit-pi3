/* Link-satisfying stub for chpass.tproj/pw_copy.c's own real
 * `extern char *tempname;` reference (DAR-174) -- pw_copy.c (real,
 * unmodified Apple source, already built here, see build_chpass.sh)
 * expects a global `char *tempname` it can read on its own error path
 * (`pw_error(tempname, 1, 1);`). That is a DIFFERENT real symbol than
 * chpass_pw_util_freebsd.c's own `static char tempname[PATH_MAX]`
 * (different storage class -- not exported -- and a different real
 * type, a fixed array vs. a pointer): pw_copy.c was evidently written
 * against an older/different real pw_util.c generation than the one
 * this project's other chpass.tproj files (chpass.c/edit.c) call
 * through chpass_pw_util_freebsd.c. Confirmed real, not invented.
 *
 * pw_copy() itself is dead code in this exact build: its only real
 * caller is chpass.c's own non-OPEN_DIRECTORY tail, which has
 * `exit(0);` as its literal first statement (chpass.c line 388, checked
 * directly) -- unconditionally unreachable before pw_copy() is ever
 * called. This definition exists only so the link succeeds; its real
 * value is never read at runtime by any code path this build can
 * actually reach. See build_chpass.sh's own header and
 * chpass_pw_scan_freebsd.c's header for the fuller writeup of this same
 * real, confirmed dead-code family.
 */
char *tempname = "";
