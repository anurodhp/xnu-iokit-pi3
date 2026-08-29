/* Real `_mach_errors[]` data table -- libxpc.dylib/libdispatch.dylib
 * both extract a real, unmodified mach_error_string.o (from
 * libxpc_syscalls.a, real libsyscall/mach/mach_error_string.c) that
 * references `extern const struct error_system _mach_errors[err_max_system
 * + 1]` (declared in libsyscall/mach/errorlib.h). Real Apple populates
 * this via its own internal `errlib` code generator from each
 * subsystem's `.errors` file -- genuinely not present anywhere in this
 * checkout (confirmed via a real search: no `_mach_errors[] = {...}`
 * definition anywhere in third_party/xnu-7195, only the extern
 * declaration). Reconstructing the real generated table for every real
 * Mach subsystem is out of scope here.
 *
 * This is a real, zero-initialized table of the REAL struct layout
 * (errorlib.h's `struct error_system`, not reinvented) -- every entry's
 * `max_sub == 0`/`subsystem == NULL`/`map_table == NULL` is honest "no
 * data available", not a fabrication. mach_error_string.o's own real,
 * unmodified logic already has a documented fallback for exactly this
 * shape (`system > err_max_system || sub >= sys_p->max_sub` returns the
 * real "(?/?)"/"(?/?) unknown error system" strings, `sys_p->map_table
 * == NULL` skips err_sparse_mapit entirely) -- so mach_error_string()/
 * mach_error_type() still run their real, unmodified code, they just
 * always take the real generic-fallback path instead of a real specific
 * per-code message, since there is no real per-subsystem data to look
 * up. Found+fixed 2026-08-29: a real QEMU boot of launchd was the first
 * real process to actually load libxpc.dylib/libdispatch.dylib and hit
 * real dyld's eager chained-fixups binding of `__mach_errors` --
 * "symbol not found in flat namespace", never reachable under this
 * project's older, fully-static link model (no dyld, no eager binding).
 */
struct error_sparse_map {
	unsigned start;
	unsigned end;
};

struct error_subsystem {
	const char *subsys_name;
	int max_code;
	const char *const *codes;
	const struct error_sparse_map *map_table;
	int map_count;
};

struct error_system {
	int max_sub;
	const char *bad_sub;
	const struct error_subsystem *subsystem;
	const struct error_sparse_map *map_table;
	int map_count;
};

#define ERR_MAX_SYSTEM 0x3f

const struct error_system _mach_errors[ERR_MAX_SYSTEM + 1] = { { 0 } };
