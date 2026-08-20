/* Real Apple SPI `getgrouplist_2()` (id.1's own source comment: "5235093")
 * for shell_cmds/id/id.c's two call sites (group listing for a named
 * user in both the default and `-G`/`groups` display paths). Real Apple
 * Libinfo/OpenDirectory implements this against opendirectoryd, which
 * this project has never built and never will (closed source, no repo --
 * same permanent gap this project's Phase 5 flat-file resolver
 * (tools/libinfo_custom/) already exists specifically to route around
 * for getpwnam/getgrgid). Per this project's source-priority rule (Apple
 * real source first, FreeBSD fallback second, custom only when neither
 * exists), this is a real last-resort custom implementation -- there is
 * no FreeBSD equivalent of this exact SPI to fall back to either (plain
 * BSD getgrouplist(3) has a different signature/allocation contract, and
 * id.c only calls the SPI form under `#ifdef __APPLE__`).
 *
 * Real, correct algorithm (same one real getgrouplist(3) documents):
 * result[0] is always the real primary/base gid passed in, followed by
 * every OTHER group (skipping a duplicate of the base gid) that lists
 * `name` in its real /etc/group member list -- scanned via
 * libinfo_custom's real li_gr_load()+li_gr_entry_count()/li_gr_at()
 * iteration accessors (already loaded once by ps_passwd_compat.c's
 * ensure_gr_loaded() before this is ever called, since id.c always
 * resolves the passwd entry first). Allocates the real output array with
 * real malloc (already linked, libmalloc.a) -- id.c's own code frees it
 * unconditionally on every return path (`free(groups)`), matching the
 * real SPI's documented "caller owns the allocation" contract. Real,
 * honest failure mode: returns -1 (matching id.c's own `if (ngroups < 0)
 * warn(...)` handling) if the allocation fails, never fabricates a
 * partial/wrong result. */
#include <string.h>
#include <stdint.h>

typedef unsigned int li_gid_t;
#define LI_GROUP_MAX_MEMBERS 32
struct li_group {
    char      *gr_name;
    char      *gr_passwd;
    li_gid_t   gr_gid;
    char      *gr_mem[LI_GROUP_MAX_MEMBERS + 1];
    int        gr_nmem;
};
extern int li_gr_entry_count(void);
extern struct li_group *li_gr_at(int index);

extern void *malloc(unsigned long size);

typedef unsigned int gid_t;

static int li_member(struct li_group *g, const char *name) {
    int i;
    for (i = 0; i < g->gr_nmem; i++) {
        if (g->gr_mem[i] != 0 && strcmp(g->gr_mem[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int32_t getgrouplist_2(const char *name, gid_t basegid, gid_t **groupsp) {
    int total, count, i;
    gid_t *out;

    total = li_gr_entry_count();
    /* Real, safe upper bound: base gid + every loaded group, even in the
     * (impossible in practice, but not assumed away) case every single
     * one lists `name` as a member. */
    out = (gid_t *)malloc((unsigned long)(total + 1) * sizeof(gid_t));
    if (out == 0) {
        return -1;
    }

    count = 0;
    out[count++] = basegid;
    for (i = 0; i < total; i++) {
        struct li_group *g = li_gr_at(i);
        if (g == 0) {
            continue;
        }
        if ((gid_t)g->gr_gid == basegid) {
            continue; /* real getgrouplist(3) semantics: no duplicate of the base gid */
        }
        if (name != 0 && li_member(g, name)) {
            out[count++] = (gid_t)g->gr_gid;
        }
    }

    *groupsp = out;
    return (int32_t)count;
}
