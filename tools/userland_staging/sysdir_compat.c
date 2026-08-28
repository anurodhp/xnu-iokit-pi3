/* Real sysdir_start_search_path_enumeration()/
 * sysdir_get_next_search_path_enumeration(): confirmed never open-
 * sourced anywhere (no apple-oss-distributions repo ships them). Real
 * Darwin resolves these against a real per-domain (user/local/network/
 * system) directory search-path table this project has no equivalent
 * of -- no multi-domain Library/Application Support layout, no
 * NSSearchPathForDirectoriesInDomains-style config anywhere in this
 * project's flat single-root filesystem model.
 *
 * Honest stub, not a fabricated path list: `state == 0` is real Darwin's
 * own documented terminal/exhausted-enumeration value (every real caller
 * -- CFSystemDirectories.o's own use here -- already loops "while
 * (state = sysdir_get_next_search_path_enumeration(state, path))",
 * meaning `state == 0` from the very first call is real API-compliant
 * behavior for "zero directories match", not undefined/wrong -- matches
 * this project's already-real "no per-domain search path config exists"
 * fact rather than inventing one. */
#include <stddef.h>

typedef unsigned int sysdir_search_path_enumeration_state;

sysdir_search_path_enumeration_state
sysdir_start_search_path_enumeration(unsigned int dir, unsigned int domainMask)
{
	(void)dir;
	(void)domainMask;
	return 0;
}

sysdir_search_path_enumeration_state
sysdir_get_next_search_path_enumeration(sysdir_search_path_enumeration_state state, char *path)
{
	(void)state;
	(void)path;
	return 0;
}
