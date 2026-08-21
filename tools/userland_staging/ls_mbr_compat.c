/* Real ls(1) -e (print.c's uuid_to_name(), part of the ACL display path)
 * calls mbr_identifier_translate() to resolve a UUID to a "user:name" /
 * "group:name" string, falling back to a plain uuid_unparse_upper() on
 * any failure -- a real, already-correct fallback path in Apple's own
 * upstream source, not something this project is patching around.
 *
 * Real Apple's mbr_identifier_translate() (third_party/Libinfo/
 * membership.subproj/membership.c) is an RPC to opendirectoryd via XPC
 * (_od_rpc_call) -- this project has no opendirectoryd and no XPC/mach-
 * bootstrap service-lookup subsystem at all (see CLAUDE.md's own "no
 * opendirectoryd" line; same class of gap nohup's vprocmgr stub and
 * id/rm's ps_passwd_compat.c account-lookup shim already document).
 * Pulling in the real membership.c wholesale would need a whole
 * additional unbuilt subsystem (XPC pipes, mach bootstrap ports) just to
 * always fail anyway on a target with no opendirectoryd to answer -- so
 * this is an honest, narrow, always-fails stub matching the same real
 * failure this call would hit for real on a system with no reachable
 * opendirectoryd instance (a genuine possible runtime state upstream's
 * own error handling already accounts for), not a fabricated success.
 */

#include <sys/types.h>
#include <errno.h>

int
mbr_identifier_translate(int id_type, const void *identifier,
    size_t identifier_size, int target_type, void **result, int *rec_type)
{
	(void)id_type;
	(void)identifier;
	(void)identifier_size;
	(void)target_type;
	(void)result;
	(void)rec_type;
	errno = ENOENT;
	return (-1);
}
