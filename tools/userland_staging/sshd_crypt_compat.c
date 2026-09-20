/* crypt(3) for the sshd port (build_sshd.sh), DAR-183.
 *
 * openbsd-compat/xcrypt.c:117 calls plain crypt(password, salt) on the
 * password-auth path (auth-passwd.c's auth_password()), and crypt(3) is
 * exported by NO dylib this project has built -- confirmed with a real
 * `nm -gU` sweep over libc_build/system/*.dylib, it is genuinely absent,
 * not merely unexported somewhere.
 *
 * This project already HAS the real implementation: tools/libinfo_custom/
 * crypt_des.c, a freestanding port of Apple's own real
 * third_party/Libc/gen/crypt.c (algorithm unchanged -- see that file's
 * header for the exact port-vs-original diff), renamed to li_crypt() so
 * it cannot collide with a future real libc crypt(). It is the SAME
 * implementation that tools/pwtool writes /etc/master.passwd hashes with
 * and that login(1)/su reach through li_crypt_verify(), so routing sshd
 * here is what makes an sshd password login accept exactly the passwords
 * pwtool set -- any other crypt() would produce a different hash for the
 * same password and silently reject every real user.
 *
 * Deliberately a forwarder rather than a second copy of the algorithm:
 * one implementation, one hash format, no drift (this project's
 * repeatedly-hit duplicate-static-copy bug class).
 */

extern char *li_crypt(const char *key, const char *setting);

char *crypt(const char *key, const char *setting) {
    return li_crypt(key, setting);
}
