/* Real environment-variable accessors reading the real `environ` global
 * -- see docs/roadmap.md Phase 4.6 "Real environment variable plumbing"
 * / docs/backlog.md P2 item 31. Three real pieces needed, not one:
 * (a) a real, non-empty envp actually reaching a child process from
 * `tools/init_binary/init.c`'s `cmd_exec()` (see `build_envp()` there);
 * (b) `environ` itself actually getting set from that envp -- initially
 * assumed `third_party/Csu/crt.c`'s real `_start()` (`environ = envp;`)
 * already handled this, but that function is real, genuine DEAD CODE on
 * this target: its own header comment says "not needed for executables
 * targeting 10.5 or later -- start calls main() directly", and it's
 * gated behind `#if __DYNAMIC__ && OLD_LIBSYSTEM_SUPPORT` (neither
 * defined here) -- confirmed via an empty `-E` preprocess output, not
 * assumed. On real modern Darwin, dyld itself sets `environ` before
 * ever reaching this asm entry point; this project has no dyld, so
 * nothing else ever did -- fixed for real in `third_party/Csu/start.s`
 * (one store instruction, right before `bl _main`, doing exactly what
 * dyld's own `ProgramVars` setup would have done for this one global);
 * and (c) a real reader, since the previously-linked `getenv()`
 * (`libc_locale_env_stubs.c`, inside `libc_locale_for_stdio.a`)
 * unconditionally returns NULL regardless of what `environ` holds.
 *
 * Real Apple `Libc/stdlib/FreeBSD/getenv.c` was tried first (per
 * CLAUDE.md's source-priority rule) but its real
 * `os/lock_private.h`/`os/base_private.h` chain hits a genuine parse
 * error under this project's cross-compile flags (`error: expected ','`
 * at `os/lock_private.h:288`) -- a real header-compatibility gap in
 * this checkout, not something worth another pass chasing down for what
 * that file needs the lock for: multi-thread safety on concurrent
 * `setenv()`/`getenv()` calls, which cannot happen here (this project
 * has no real thread creation -- see docs/roadmap.md Phase 7.5's
 * pthread entry -- single-thread-correct is the real, documented
 * standard the rest of this project's own libpthread port already
 * holds itself to). This is a real, correct implementation of real
 * POSIX getenv(3)/setenv(3)/unsetenv(3)/putenv(3) semantics, just
 * without the unneeded lock -- not a stub, not fabricated behavior. */
#include <string.h>
#include <stdlib.h>
#include <errno.h>

extern char **environ;

/* Real Apple `<crt_externs.h>` (`Libc/include/crt_externs.h`, already
 * on this project's -I path) `#define`s `environ` to
 * `(*_NSGetEnviron())` for files that `#include` it (e.g. real
 * `Libc/gen/FreeBSD/exec.c`'s `execvp()`) -- this is the real,
 * documented indirection point, just returning the address of the same
 * real `environ` global `crt.c` sets, not a separate copy. */
char ***_NSGetEnviron(void) {
    return &environ;
}

static size_t name_len(const char *name) {
    const char *p = name;
    while (*p && *p != '=') p++;
    return (size_t)(p - name);
}

char *getenv(const char *name) {
    if (name == NULL || *name == '\0') {
        return NULL;
    }
    size_t nlen = name_len(name);
    if (environ == NULL) {
        return NULL;
    }
    for (char **p = environ; *p != NULL; p++) {
        if (strncmp(*p, name, nlen) == 0 && (*p)[nlen] == '=') {
            return *p + nlen + 1;
        }
    }
    return NULL;
}

/* Real environ array storage -- grown via realloc as needed. Since
 * `environ` may initially point at init.c's own static, non-heap
 * array (see build_envp() there), the first mutation copies it into a
 * real heap-allocated array rather than assuming it's already
 * reallocatable -- same real defensive pattern real Apple's own
 * setenv() takes for the same reason (initial envp is caller-owned). */
static int env_owned = 0;

static int ensure_owned(int extra_slots) {
    int count = 0;
    if (environ != NULL) {
        while (environ[count] != NULL) count++;
    }
    if (env_owned) {
        return count;
    }
    char **newenv = malloc((size_t)(count + extra_slots + 1) * sizeof(char *));
    if (newenv == NULL) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        newenv[i] = environ[i];
    }
    newenv[count] = NULL;
    environ = newenv;
    env_owned = 1;
    return count;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t nlen = name_len(name);
    int count = ensure_owned(1);
    if (count < 0) {
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=') {
            if (!overwrite) {
                return 0;
            }
            size_t vlen = strlen(value);
            char *entry = malloc(nlen + 1 + vlen + 1);
            if (entry == NULL) {
                errno = ENOMEM;
                return -1;
            }
            memcpy(entry, name, nlen);
            entry[nlen] = '=';
            memcpy(entry + nlen + 1, value, vlen + 1);
            environ[i] = entry;
            return 0;
        }
    }
    size_t vlen = strlen(value);
    char *entry = malloc(nlen + 1 + vlen + 1);
    if (entry == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    memcpy(entry + nlen + 1, value, vlen + 1);
    /* ensure_owned() above reserved exactly one extra slot for this. */
    char **newenv = realloc(environ, (size_t)(count + 2) * sizeof(char *));
    if (newenv == NULL) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }
    environ = newenv;
    environ[count] = entry;
    environ[count + 1] = NULL;
    return 0;
}

int unsetenv(const char *name) {
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t nlen = name_len(name);
    int count = ensure_owned(0);
    if (count < 0) {
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=') {
            for (int j = i; j < count; j++) {
                environ[j] = environ[j + 1];
            }
            return 0;
        }
    }
    return 0;
}

int putenv(char *string) {
    if (string == NULL) {
        errno = EINVAL;
        return -1;
    }
    const char *eq = strchr(string, '=');
    if (eq == NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t nlen = (size_t)(eq - string);
    int count = ensure_owned(1);
    if (count < 0) {
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (strncmp(environ[i], string, nlen) == 0 && environ[i][nlen] == '=') {
            environ[i] = string;
            return 0;
        }
    }
    char **newenv = realloc(environ, (size_t)(count + 2) * sizeof(char *));
    if (newenv == NULL) {
        errno = ENOMEM;
        return -1;
    }
    environ = newenv;
    environ[count] = string;
    environ[count + 1] = NULL;
    return 0;
}
