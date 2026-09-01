/* Extends asl_client_compat.c's already-established honest ASL shim
 * (see that file's own header for the full "no bootstrap-namespace
 * registrar yet, so nothing would ever answer asl_open()'s real
 * bootstrap_look_up2() lookup" reasoning -- same standing rule applies
 * here, not re-litigated) with the additional real ASL entry points
 * real, unmodified third_party/Libc/gen/utmpx-darwin.c actually calls
 * (getlastlogx()/getlastlogxbyname()/pututxline()'s own ASL-backed
 * wtmp/lastlog storage -- confirmed via a real grep of that file for
 * every `asl_*(` callsite, not guessed): asl_append, asl_get, asl_match,
 * asl_next, asl_release, asl_set_query.
 *
 * Real signatures cross-checked directly against this project's own
 * pinned third_party/syslog/libsystem_asl.tproj/include/asl.h (not
 * guessed) so callers see exactly the real prototypes the real SDK
 * declares.
 *
 * Same honest-no-op contract as asl_client_compat.c's own functions:
 * this project has no real ASL store to query or write to, so every
 * "read" path here (asl_get/asl_next/asl_match) returns the real,
 * correct answer for "there is nothing there" (NULL / empty), and every
 * "write" path (asl_append/asl_set_query/asl_release) is a real no-op
 * returning success. Concretely: utmpx-darwin.c's getlastlogxbyname()
 * calls asl_match(NULL, query, ...) expecting NULL back when no matching
 * message exists -- returning NULL here is not a fabricated shortcut,
 * it is literally correct given this project's total absence of stored
 * ASL messages (nothing was ever asl_send()'d for real, see
 * asl_client_compat.c's own asl_send() no-op), so login's own
 * dolastlog() correctly falls through its `if (*lastlog.ll_line)` check
 * and simply prints nothing, exactly like a real system's first-ever
 * login for an account with no prior lastlogx record. */
#include <stddef.h>
#include <stdint.h>

typedef struct __asl_object_s *asl_object_t;

void asl_append(asl_object_t obj, asl_object_t obj_to_add) {
    (void)obj;
    (void)obj_to_add;
}

const char *asl_get(asl_object_t msg, const char *key) {
    (void)msg;
    (void)key;
    return NULL;
}

asl_object_t asl_match(asl_object_t data, asl_object_t querylist, size_t *last,
                        size_t start, size_t count, uint32_t duration, int32_t direction) {
    (void)data;
    (void)querylist;
    (void)start;
    (void)count;
    (void)duration;
    (void)direction;
    if (last) {
        *last = 0;
    }
    return NULL; /* real, correct answer: no stored messages ever match */
}

asl_object_t asl_next(asl_object_t obj) {
    (void)obj;
    return NULL;
}

void asl_release(asl_object_t obj) {
    (void)obj;
}

int asl_set_query(asl_object_t msg, const char *key, const char *value, uint32_t op) {
    (void)msg;
    (void)key;
    (void)value;
    (void)op;
    return 0;
}
