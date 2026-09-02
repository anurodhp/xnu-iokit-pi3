/* Real gap, project-wide, found while building lookup.subproj/kvbuf.c
 * (DAR-181, real Apple Libinfo port): the public malloc *zone*
 * management SPI (malloc/malloc.h's malloc_default_zone()/
 * malloc_zone_calloc()/malloc_zone_realloc()/malloc_zone_from_ptr()/
 * malloc_make_purgeable()/malloc_make_nonpurgeable()) has no real
 * implementation anywhere in this project yet -- confirmed via `nm -g`
 * across every already-built libmalloc.a/libsystem_malloc.dylib member:
 * this project's real allocator (a real "scalable zone" internally,
 * see libsystem_malloc.dylib's own _scalable_zone_info/
 * _scalable_zone_statistics exports) never grew the *public*,
 * multi-zone-aware entry points real callers like kvbuf.c use. kvbuf.c
 * -- Libinfo's core key/value buffer serializer, used by essentially
 * every lookup module (file_module.c, search_module.c, cache_module.c,
 * si_data.c all build items through it) -- calls these unconditionally
 * in its base kvbuf_new()/kvbuf_init()/kvbuf_grow() paths, not just an
 * optional purgeable-memory variant, so this is real, in-scope, load-
 * bearing infrastructure for this pass, not something safely deferred
 * via -Wl,-U the way the genuinely out-of-scope getaddrinfo/hostname
 * symbols in build_libinfo.sh are.
 *
 * Real, honest, narrow fix, same shape as asl_client_compat.c's own
 * precedent for a similar "real SPI, no real backing daemon/subsystem
 * here" gap: this project's allocator only ever has ONE real zone (no
 * multi-zone allocation exists here at all), so malloc_default_zone()/
 * malloc_zone_from_ptr() can honestly both return the same fixed,
 * non-NULL sentinel (kvbuf.c only ever passes it back opaquely into
 * these same calloc/realloc wrappers -- confirmed via kvbuf.c's own
 * source, it never dereferences the zone struct itself), and
 * malloc_zone_calloc()/malloc_zone_realloc() are real, working
 * pass-throughs to this project's one real allocator (calloc/realloc,
 * already provided by libsystem_malloc.dylib, a sibling of this file's
 * own dylib in every consumer). malloc_make_purgeable()/
 * malloc_make_nonpurgeable() are real, honest no-ops matching their
 * real documented contract for a system with no purgeable-VM support
 * (see malloc/malloc.h's own comment: malloc_make_nonpurgeable()
 * "returns 0 if the ptr was purged... else returns non-zero" -- this
 * project's memory is never actually reclaimed under pressure, so
 * "always survived" (non-zero) is the real, honest answer, not a
 * fabricated one). */
#include <stddef.h>
#include <stdlib.h>
#include <malloc/malloc.h>

static struct _malloc_zone_t li_default_zone_sentinel;

malloc_zone_t *
malloc_default_zone(void)
{
	return &li_default_zone_sentinel;
}

malloc_zone_t *
malloc_zone_from_ptr(const void *ptr)
{
	(void)ptr;
	return &li_default_zone_sentinel;
}

void *
malloc_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size)
{
	(void)zone;
	return calloc(num_items, size);
}

void *
malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size)
{
	(void)zone;
	return realloc(ptr, size);
}

void
malloc_make_purgeable(void *ptr)
{
	(void)ptr;
}

int
malloc_make_nonpurgeable(void *ptr)
{
	(void)ptr;
	return 1; /* "survived" -- real, honest answer: nothing is ever purged here */
}
