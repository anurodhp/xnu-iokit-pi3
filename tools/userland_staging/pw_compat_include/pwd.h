/* pw(8) build-wide augmentation of <pwd.h> (DAR-341).
 *
 * pw_user.c's own real, unmodified pw_user_del() (2 identical guard
 * blocks, confirmed via exhaustive grep -- no other real call site in
 * this checkout) reads `pwd->pw_fields & _PWF_SOURCE` to decide whether
 * an account came from local files or NIS before allowing deletion.
 * Real Apple struct passwd (this project's iPhoneOS SDK pwd.h, confirmed
 * via direct read) has no pw_fields member at all -- a private BSD/
 * Darwin extension recording which backend (of potentially several,
 * transparently merged by real getpwent()) sourced a given entry.
 *
 * Unlike chpass_compat_include/libutil.h's own `#define pw_fields
 * pw_gid` (DAR-174, safe ONLY there because that call site is
 * unreachable dead code, per that file's own header) -- pw_user.c's
 * pw_fields reads are LIVE, real code on `pw userdel`'s actual deletion
 * path. Aliasing to pw_gid here would be a real, live bug (verified:
 * `(gid_value & 0x3000)` is 0 for essentially every real gid, making the
 * guard's `!= _PWF_FILES` branch true for every single account and
 * refusing every deletion -- checked directly, not assumed, before
 * ruling this out; DAR-313 requires verifying a shim's precedent still
 * applies, not reusing it transitively).
 *
 * The real, structural fact this project actually has: its ENTIRE
 * account backend (tools/libinfo_custom/) is flat-file only --
 * li_pw_load() always and only parses /etc/master.passwd, there is no
 * NIS client anywhere in this checkout. So the true, correct answer to
 * "which source did this passwd entry come from" is unconditionally
 * "files," for every real account, always -- not a guess, a fact about
 * this project's own resolver. Rather than aliasing pw_fields to an
 * existing field's real (irrelevant, and in pw_gid's case actively
 * wrong) value, _PWF_SOURCE/_PWF_FILES/_PWF_NIS (real FreeBSD's own
 * private struct-passwd-source bits, confirmed via a real `git show`
 * against this project's pinned third_party/freebsd-src @ release/13.2.0
 * include/pwd.h: 0x3000/0x1000/0x2000) are redefined here so the mask
 * degenerates correctly regardless of which field pw_fields aliases:
 * _PWF_SOURCE=0 makes `X & _PWF_SOURCE` always 0 for ANY real field X,
 * and _PWF_FILES=0/_PWF_NIS=1 (nonzero, never matched) make that always
 * compare equal to "files," never to "NIS" -- exactly the real, correct
 * answer for a files-only deployment, not a coincidence. pw_fields
 * itself is aliased to pw_gid purely so `pwd->pw_fields` parses (C's
 * `->` operator requires a real member name) -- its actual numeric value
 * is masked to irrelevance by _PWF_SOURCE=0 above, unlike chpass's case
 * where the alias mattered only because the code was unreachable.
 */
#ifndef _PW_PWD_AUGMENT_H_
#define _PW_PWD_AUGMENT_H_

#include_next <pwd.h>

#define pw_fields pw_gid
#define _PWF_SOURCE 0
#define _PWF_FILES  0
#define _PWF_NIS    1

#endif
