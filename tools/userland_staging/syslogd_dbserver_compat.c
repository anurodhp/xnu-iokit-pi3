/* Real gap, found during build_syslogd.sh's own trial link (not
 * anticipated by build_syslogd_recon.sh's -fsyntax-only pass, which only
 * proves each file compiles standalone, not that cross-file references
 * resolve): syslogd.c's real main() (dispatch_async(^{ database_server();
 * }) at daemon startup), daemon.c's aslmsg_verify(), asl_action.c's
 * __send_to_asl_store(), and remote.c's session() all have direct,
 * unconditional static references into dbserver.c -- the real live
 * ASL query/ingest Mach/XPC service, out of scope for this pass (needs a
 * real asl_ipc.h MIG regen from aslcommon/asl_ipc.defs, not attempted;
 * see docs/daemon-roadmap.md Phase 6.7 / docs/backlog.md item 17).
 *
 * Two different real answers, not one blanket stub:
 *
 * db_save_message()/disaster_message() are genuinely NOT live-query/Mach
 * code -- reading dbserver.c's own real bodies (syslogd.tproj/dbserver.c:
 * 470/561) shows both are just persistence calls into the ASL storage
 * layer (asl_memory_save()/asl_memory_open(), real, already compiled --
 * asl_memory.c is one of this pass's own 10 clean files) plus an
 * optional real file-backed store path (asl_store_save(), in
 * libsystem_asl.tproj/src/asl_store.c, NOT compiled this pass -- real,
 * separate future work). This project's own real syslogd.tproj/daemon.h
 * global_s struct (memory_db/dbtype/db_memory_max/db_memory_str_max) is
 * already real and shared by every file this pass does compile, so a
 * real, correct, memory-backed implementation is small and honest here
 * -- messages routed to a "store" action are genuinely persisted (to the
 * in-process ASL memory ring buffer), just not to the on-disk binary
 * store yet. This is NOT a silent-drop stub.
 *
 * database_server()/db_query()/remove_lockdown_session() ARE the real
 * live-query-service/network-remote-forwarding surface itself (session
 * management, MIG dispatch, lockdown-mode bookkeeping) -- there is no
 * smaller real subset of these to implement without the actual
 * asl_ipc.h MIG regen this pass deliberately doesn't attempt. Honest
 * no-ops: database_server() never launches the live-query listener
 * thread (real behavior difference: `syslog -w`-style live tail queries
 * and remote network log forwarding don't work yet -- exactly the
 * already-documented, deliberately-deferred scope, not a new gap),
 * db_query() reports zero results, remove_lockdown_session() has
 * nothing to clean up since no lockdown session is ever established.
 */
#include "daemon.h"
#include <stdint.h>

extern uint32_t asl_memory_open(uint32_t max_records, size_t max_str_mem, asl_memory_t **s);
extern uint32_t asl_memory_save(asl_memory_t *s, asl_msg_t *msg, uint64_t *mid);

#define ASL_STATUS_OK 0
#define DB_TYPE_MEMORY 2

void db_save_message(asl_msg_t *msg) {
    if (global.memory_db == NULL) {
        uint32_t status = asl_memory_open(global.db_memory_max, global.db_memory_str_max, &(global.memory_db));
        if (status != ASL_STATUS_OK) return;
        global.dbtype |= DB_TYPE_MEMORY;
    }
    uint64_t msgid = 0;
    asl_memory_save(global.memory_db, msg, &msgid);
}

void disaster_message(asl_msg_t *msg) {
    if ((global.dbtype & DB_TYPE_MEMORY) == 0 && global.memory_db == NULL) {
        uint32_t status = asl_memory_open(global.db_memory_max, global.db_memory_str_max, &(global.memory_db));
        if (status != ASL_STATUS_OK) return;
    }
    if (global.memory_db != NULL) {
        uint64_t msgid = 0;
        asl_memory_save(global.memory_db, msg, &msgid);
    }
}

void database_server(void) {
    /* real no-op: live ASL query/ingest service not built this pass */
}

uint32_t db_query(asl_msg_list_t *query, asl_msg_list_t **res, uint64_t startid, int count,
                   uint32_t duration, int direction, uint64_t *lastid,
                   int32_t ruid, int32_t rgid, int raccess) {
    (void)query; (void)startid; (void)count; (void)duration; (void)direction;
    (void)ruid; (void)rgid; (void)raccess;
    if (res) *res = NULL;
    if (lastid) *lastid = 0;
    return ASL_STATUS_OK;
}

void remove_lockdown_session(int fd) {
    (void)fd;
}

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
