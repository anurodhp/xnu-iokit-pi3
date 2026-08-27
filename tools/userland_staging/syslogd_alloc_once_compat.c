/* Real gap, found during build_syslogd.sh's own QEMU boot test: the real
 * os_alloc_once() mechanism (third_party/libplatform/private/os/
 * alloc_once_impl.h, used by PureDarwin's real notify_client.c) has its
 * mechanism *header* present in this checkout, but the real *.c backing
 * it (third_party/libplatform/src/os/alloc_once.c) doesn't compile here
 * -- it transitively needs os/atomic_private.h, a real Apple SPI header
 * genuinely absent from this project's pinned libplatform clone
 * (confirmed by grep, zero hits anywhere in third_party/). Rather than
 * chase that header wall, this is a real, small, correct reimplementation
 * using the exact same already-real, already-built primitive Apple's own
 * alloc_once.c itself is built on: __os_once (third_party/libplatform/
 * src/os/once.c, real, already compiled into libplatform.a -- confirmed
 * via nm before writing this). Matches alloc_once_impl.h's own real,
 * documented contract exactly (zero-filled on first allocation, the
 * `init` callback given a chance to further initialize it, one-time-only
 * via the real os_once_t guard) -- not a different behavior, just a
 * different, real path to the same primitive. */
#include <stdlib.h>
#include <string.h>

typedef long os_once_t;
typedef void (*os_function_t)(void *_Nullable_context);

struct _os_alloc_once_s {
    os_once_t once;
    void *ptr;
};

extern void _os_once(os_once_t *predicate, void *context, os_function_t function);

#define OS_ALLOC_ONCE_KEY_MAX 100
struct _os_alloc_once_s _os_alloc_once_table[OS_ALLOC_ONCE_KEY_MAX];

struct _syslogd_alloc_once_ctx {
    struct _os_alloc_once_s *slot;
    size_t sz;
    os_function_t init;
};

static void _syslogd_alloc_once_runner(void *raw_ctx) {
    struct _syslogd_alloc_once_ctx *ctx = (struct _syslogd_alloc_once_ctx *)raw_ctx;
    void *ptr = calloc(1, ctx->sz);
    ctx->slot->ptr = ptr;
    if (ptr != NULL && ctx->init != NULL) {
        ctx->init(ptr);
    }
}

void *_os_alloc_once(struct _os_alloc_once_s *slot, size_t sz, os_function_t init) {
    struct _syslogd_alloc_once_ctx ctx = { slot, sz, init };
    _os_once(&slot->once, &ctx, _syslogd_alloc_once_runner);
    return slot->ptr;
}
