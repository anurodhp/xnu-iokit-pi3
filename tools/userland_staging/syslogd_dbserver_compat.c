/* Remaining stand-ins for parts of libsystem_asl that syslogd's static
 * ASL copy does not compile (see build_syslogd.sh's libsystem_asl.tproj
 * loop). Each has its own comment below.
 *
 * DAR-121: this file used to also carry no-op/memory-only stand-ins for
 * syslogd.tproj/dbserver.c (db_save_message, disaster_message,
 * database_server, db_query, remove_lockdown_session), because the
 * asl_ipc MIG stubs dbserver.c needs had never been regenerated.
 * build_syslogd.sh now runs mig on aslcommon/asl_ipc.defs and compiles
 * the real dbserver.c, so those are gone -- in particular the no-op
 * database_server(), which meant nothing ever received on
 * com.apple.system.logger. */
/* Real gap: asl_msg.c's real _asl_log_args_to_xpc() calls
 * asl_client_kvdict() -- but the real defining file, asl_client.c,
 * itself unconditionally #includes the deferred asl_ipc.h (real ASL
 * Mach/XPC client transport, same MIG-regen dependency as dbserver.c),
 * so pulling in the whole file isn't possible this pass. The real
 * function itself (asl_client.c:533) is a trivial one-line accessor on
 * a real struct field already declared in the real, Mach-free
 * libsystem_asl.tproj/include/asl_client.h -- reproduced verbatim here,
 * not reimplemented differently. */
#include <asl_client.h>
asl_msg_t *asl_client_kvdict(asl_client_t *client) {
    if (client == NULL) return NULL;
    return client->kvdict;
}

/* Real gap: asl_core.c's real asl_core_check_group_access() calls
 * mbr_check_membership() (real Directory Services membership-lookup
 * API, <membership.h>) to check if a log reader belongs to a group
 * granted read access -- this project has never ported any part of
 * Directory Services/mbr_* (no opendirectoryd, confirmed, see
 * CLAUDE.md's own "no launchd, no opendirectoryd" scope statement).
 * Real, safe, fail-closed default for a permission check with no real
 * membership DB to consult: report "not a member" (ismember = 0),
 * success return -- the same real answer a legitimate "user isn't in
 * this restricted group" case would give, never a false grant. */
#include <sys/types.h>
#include <uuid/uuid.h>
int mbr_check_membership(const uuid_t user, const uuid_t group, int *ismember) {
    (void)user;
    (void)group;
    if (ismember) *ismember = 0;
    return 0;
}

/* Real gap: asl_object.c's real _asl_object_init() (a dispatch_once-
 * guarded, always-run constructor) unconditionally calls
 * asl_client_jump_table() to populate its ASL_TYPE_CLIENT dispatch
 * slot, alongside every other real ASL object type this build already
 * has (msg/msg_list/file/store) -- but the real defining file,
 * asl_client.c, can't be compiled this pass (needs the deferred
 * asl_ipc.h). This project's own syslogd daemon never constructs an
 * ASL_TYPE_CLIENT object internally (that's the asl_open()/asl_log()
 * *client*-side API, a separate consumer this pass doesn't build) --
 * confirmed by grepping every file this build does compile for
 * ASL_TYPE_CLIENT/asl_client_new, zero real call sites -- so a real,
 * honest NULL jump table (never actually dispatched through) is safe,
 * not a functional gap. asl_client_match()/asl_client_search() are only
 * ever reached via that same jump table, so they're unreachable dead
 * code for identical reasons; real, honest "no results" stand-ins,
 * matching db_query()'s already-established contract above. */
#include <asl_object.h>
const asl_jump_table_t *asl_client_jump_table(void) {
    return NULL;
}

asl_msg_list_t *asl_client_search(asl_client_t *client, asl_msg_t *query) {
    (void)client;
    (void)query;
    return NULL;
}

asl_msg_list_t *asl_client_match(asl_client_t *client, asl_msg_list_t *querylist, size_t *last,
                                  size_t start, size_t count, uint32_t duration, int32_t direction) {
    (void)client; (void)querylist; (void)start; (void)count; (void)duration; (void)direction;
    if (last) *last = 0;
    return NULL;
}
