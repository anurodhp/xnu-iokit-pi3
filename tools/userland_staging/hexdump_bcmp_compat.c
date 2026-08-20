/* bcmp: real BSD semantics ("0 if equal, nonzero otherwise") are a
 * strict subset of memcmp's -- same real, standard port
 * copyfile_syscalls.c's own identical bcmp() already established for
 * this project (real memcmp(a,b,n)==0 iff bcmp(a,b,n)==0 for any
 * conforming implementation). Kept in its own file rather than
 * block3_last_resort_compat.c: hexdump.c is Block 3's only real bcmp()
 * caller, and other Block 3 commands sharing that file (mktemp, du)
 * also link libcopyfile.a, which already defines its own bcmp() --
 * a real duplicate-symbol link error confirmed this conflict, not
 * guessed preemptively. */
typedef unsigned long size_t;
extern int memcmp(const void *a, const void *b, size_t n);
int bcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}
