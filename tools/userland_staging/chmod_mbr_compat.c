/* mbr_identifier_to_uuid: real chmod_acl.c's `+a`/ACL-by-name path
 * resolves a "user:name"/"group:name" ACL qualifier to a UUID via the
 * real membership service (opendirectoryd-backed on real macOS). Same
 * real "no directory service at all" gap copyfile_syscalls.c's own
 * mbr_uid_to_uuid/mbr_gid_to_uuid/mbr_uuid_to_id stand-ins already
 * document for this project -- this is the fourth member of that same
 * real mbr_*() family, not duplicated into copyfile_syscalls.c itself
 * since chmod doesn't otherwise link that file. Honest failure (real
 * macOS's own contract for "cannot resolve"), not a fabricated UUID --
 * chmod's own plain mode-bit path (this command's actual common case)
 * never reaches this at all. */
typedef unsigned char uuid_t[16];
typedef int id_t;

int mbr_identifier_to_uuid(int id_type, const void *identifier, unsigned int id_len, uuid_t uu) {
    (void)id_type;
    (void)identifier;
    (void)id_len;
    (void)uu;
    return -1;
}
