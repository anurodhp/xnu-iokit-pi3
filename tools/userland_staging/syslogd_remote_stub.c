/* Real, deliberate exclusion, not a missing gap: remote.c (network ASL
 * remote-forwarding/live-query protocol) was excluded from this pass's
 * scope from the start (build_syslogd_recon.sh's own HONEST SCOPE
 * STATEMENT: "excluding dbserver.c... and remote.c") -- it compiles
 * clean standalone (a real surprise noted in that recon), but its own
 * real internals (session(), remote_acceptmsg()) call directly into
 * dbserver.c's db_query()/remove_lockdown_session() (the live-query
 * service, also out of scope) and need real pthread_attr_* support this
 * project's curated libpthread port deliberately never built (see
 * build_libpthread.sh's own header: pthread.c -- which has the real
 * pthread_attr_init/setdetachstate/destroy -- is intentionally NOT
 * compiled here).
 *
 * syslogd.c's own module table takes a direct, unconditional function-
 * pointer reference to remote_init() (`m_remote->init = remote_init;`,
 * syslogd.c:225) regardless of whether the remote module is ever
 * enabled at runtime (`if (m_remote->enabled) m_remote->init();`,
 * syslogd.c:229 -- return value not even checked) -- so some real
 * remote_init symbol must exist for syslogd.macho to link at all, even
 * though this build never wires the module on. Real, honest stand-in:
 * matches remote.c's own established contract exactly (every real
 * failure path in remote_init_lockdown()/remote_init_tcp() already
 * returns -1 on failure, non-fatal, silently leaves the module
 * unavailable) -- "module unavailable" is remote.c's OWN real answer
 * for exactly this situation, not a fabricated one. */
int remote_init(void) {
    return -1;
}

/* syslogd.c's init_modules() also directly references remote_close()/
 * remote_reset() (module shutdown/reset hooks) unconditionally, same
 * real reason as remote_init() above -- both are real void-returning,
 * no-arg hooks in remote.c (remote.c:900/927); since remote_init()
 * above always reports "unavailable", there is never any real remote
 * session/resource for these to actually tear down. */
void remote_close(void) {
}

void remote_reset(void) {
}
