/*
 * Freestanding PID-1 init/shell for XNU-on-rpi3, booted via mockfs.
 *
 * No libc, no dyld: built as a fully static Mach-O with LC_UNIXTHREAD and
 * zero LC_LOAD_DYLIB commands (see build.py) so the kernel exec path jumps
 * straight to _start without ever invoking dyld. All BSD syscalls below are
 * issued directly (`svc #0x80`, number in x16, arm64 Darwin convention) --
 * numbers taken from third_party/xnu/bsd/kern/syscalls.master, not guessed.
 *
 * Every "command" here is a builtin dispatched by name, and the whole
 * directory tree is a virtual, in-process structure -- NOT the real VFS.
 * That's not a shortcut, it's the only option right now: mockfs (the root
 * fs pid 1 execs from) implements only lookup/read/getattr on 3 fixed
 * names (see bsd/miscfs/mockfs/mockfs_vnops.c), with no VNOP_MKDIR/
 * VNOP_CREATE at all, and devfs (auto-mounted at /dev by mockfs_mountroot)
 * supports mkdir but has no VNOP_CREATE for regular files either -- there
 * is nowhere in the real, currently-compiled-in VFS to persist a directory
 * tree or separate command binaries yet. Real dyld/Libc + real coreutils
 * are a later, separate pull once we're off this bootstrap milestone (see
 * project memory: userspace libc/dyld/launchd).
 */

typedef unsigned long size_t;
typedef long ssize_t;

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ---- */

#define SYS_exit  1
#define SYS_read  3
#define SYS_write 4
#define SYS_open  5
#define SYS_close 6
#define SYS_dup2  90

#define O_RDWR 2

#ifdef INIT_HOST_TEST
/* Host-side logic testing only (see build.py) -- never compiled into the
 * actual freestanding target binary. Real syscall numbers happen to match
 * host libc's read/write/exit, so this just forwards to them. */
long host_shim_syscall3(long num, long a0, long a1, long a2);
static inline long syscall3(long num, long a0, long a1, long a2) {
    return host_shim_syscall3(num, a0, a1, a2);
}
#else
static inline long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80"
                 : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    return x0;
}
#endif

static ssize_t sys_write(int fd, const void *buf, size_t n) {
    return syscall3(SYS_write, fd, (long)buf, (long)n);
}

static ssize_t sys_read(int fd, void *buf, size_t n) {
    return syscall3(SYS_read, fd, (long)buf, (long)n);
}

static void sys_exit(int code) {
    syscall3(SYS_exit, code, 0, 0);
    for (;;) { }
}

static int sys_open(const char *path, int flags, int mode) {
    return (int)syscall3(SYS_open, (long)path, flags, mode);
}

static int sys_close(int fd) {
    return (int)syscall3(SYS_close, fd, 0, 0);
}

static int sys_dup2(int from, int to) {
    return (int)syscall3(SYS_dup2, from, to, 0);
}

/* The kernel hands pid 1 an empty fd table -- on real macOS, launchd itself
 * is the one that opens /dev/console and dup2's it onto 0/1/2 before doing
 * anything else; there is no kernel-side setup. We're standing in for
 * launchd, so we have to do the same thing explicitly, or every read(0)/
 * write(1) call below is just an EBADF against a nonexistent fd -- which is
 * exactly what happened before this fix: sys_write() ignores its return
 * value (see put()), so the banner "printed" into the void, and
 * read_line()'s `if (n < 0) continue;` turned the following prompt loop
 * into a silent, fast, infinite EBADF-retry spin that looked from the
 * outside (no console output, PC parked right after the read's `svc`) like
 * a hang. devfs always creates /dev/console (major/minor 0,0, the real
 * cnopen/km.c console device) via devfs_init() - not board- or
 * mockfs-specific - so this works unconditionally once devfs is mounted. */
/* DIAGNOSTIC: externally inspectable via QEMU monitor (nm this binary for
 * the symbol's address, then `xp/1xw <addr>` on the guest VA) since we
 * can't trust console output to debug console output. Remove once the
 * console-routing question is settled. */
volatile int g_console_open_result = -999;
volatile long g_first_write_result = -999;
static int g_write_call_count = 0;

static void open_console(void) {
    int fd = sys_open("/dev/console", O_RDWR, 0);
    g_console_open_result = fd;
    if (fd < 0) {
        return; /* nothing we can do without a console; fall through as before */
    }
    if (fd != 0) {
        sys_dup2(fd, 0);
    }
    sys_dup2(fd, 1);
    sys_dup2(fd, 2);
    if (fd > 2) {
        sys_close(fd);
    }
}

/* ---------------------------- tiny string helpers ----------------------- */

static size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int kstreq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void kmemcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

static void kstrncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void put(const char *s) {
    ssize_t r = sys_write(1, s, kstrlen(s));
    if (g_write_call_count == 0) {
        g_first_write_result = r;
    }
    g_write_call_count++;
}

static void putn(const char *s, size_t n) {
    sys_write(1, s, n);
}

/* ------------------------------- virtual VFS ----------------------------- */

#define MAX_NODES    64
#define MAX_NAME     32
#define MAX_CHILDREN 16
#define MAX_PATH     256

typedef struct node {
    char name[MAX_NAME];
    int is_dir;
    int parent;
    const char *content;
    int children[MAX_CHILDREN];
    int nchildren;
} node_t;

static node_t nodes[MAX_NODES];
static int node_count;

static int new_node(const char *name, int is_dir, int parent, const char *content) {
    int idx = node_count++;
    node_t *n = &nodes[idx];
    kstrncpy(n->name, name, MAX_NAME);
    n->is_dir = is_dir;
    n->parent = parent;
    n->content = content;
    n->nchildren = 0;
    if (parent >= 0 && nodes[parent].nchildren < MAX_CHILDREN) {
        nodes[parent].children[nodes[parent].nchildren++] = idx;
    }
    return idx;
}

#define ROOT 0

static void vfs_init(void) {
    node_count = 0;
    new_node("/", 1, -1, 0); /* ROOT == 0 */

    int bin = new_node("bin", 1, ROOT, 0);
    new_node("ls",    0, bin, "builtin: list directory contents\n");
    new_node("cat",   0, bin, "builtin: print file contents\n");
    new_node("cd",    0, bin, "builtin: change directory\n");
    new_node("pwd",   0, bin, "builtin: print working directory\n");
    new_node("echo",  0, bin, "builtin: print arguments\n");
    new_node("mkdir", 0, bin, "builtin: create a directory\n");
    new_node("help",  0, bin, "builtin: list available commands\n");
    new_node("clear", 0, bin, "builtin: clear the screen\n");
    new_node("exit",  0, bin, "builtin: exit the shell (halts pid 1)\n");
    new_node("bash",  0, bin, "builtin: this shell, addressed by its other name\n");

    int sbin = new_node("sbin", 1, ROOT, 0);
    new_node("init", 0, sbin, "builtin: PID 1 -- this program\n");

    int etc = new_node("etc", 1, ROOT, 0);
    new_node("motd", 0, etc,
        "Welcome to XNU/rpi3.\n"
        "This is a freestanding PID-1 shell: no dyld, no libc, no real\n"
        "filesystem yet. This tree lives entirely in this process.\n");

    /* mockfs_mountroot() auto-mounts real devfs here once the kernel boots
     * with mockfs enabled -- this placeholder just makes `ls`/`cd` sane
     * before that lands / if it's not enabled in a given build. */
    new_node("dev", 1, ROOT, 0);
    new_node("tmp", 1, ROOT, 0);
    new_node("var", 1, ROOT, 0);
}

/* Resolve a (possibly relative) path against `cwd`. Returns node index or -1. */
static int vfs_resolve(int cwd, const char *path) {
    int cur = (path[0] == '/') ? ROOT : cwd;
    char buf[MAX_PATH];
    kstrncpy(buf, path, MAX_PATH);

    char *p = buf;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        int seglen = (int)(p - start);
        if (*p) *p++ = 0;

        if (seglen == 0) continue;
        if (start[0] == '.' && start[1] == 0) continue;
        if (start[0] == '.' && start[1] == '.' && start[2] == 0) {
            if (nodes[cur].parent >= 0) cur = nodes[cur].parent;
            continue;
        }

        if (!nodes[cur].is_dir) return -1;
        int found = -1;
        for (int i = 0; i < nodes[cur].nchildren; i++) {
            int c = nodes[cur].children[i];
            if (kstreq(nodes[c].name, start)) { found = c; break; }
        }
        if (found < 0) return -1;
        cur = found;
    }
    return cur;
}

/* Write the absolute path of node `idx` into buf. */
static void vfs_path(int idx, char *buf, size_t bufsz) {
    if (idx == ROOT) {
        kstrncpy(buf, "/", bufsz);
        return;
    }
    char stack_names[16][MAX_NAME];
    int depth = 0;
    int cur = idx;
    while (cur != ROOT && depth < 16) {
        kstrncpy(stack_names[depth], nodes[cur].name, MAX_NAME);
        depth++;
        cur = nodes[cur].parent;
    }
    size_t off = 0;
    for (int i = depth - 1; i >= 0 && off + 1 < bufsz; i--) {
        buf[off++] = '/';
        size_t l = kstrlen(stack_names[i]);
        if (off + l >= bufsz) l = bufsz - off - 1;
        kmemcpy(buf + off, stack_names[i], l);
        off += l;
    }
    buf[off] = 0;
}

/* ------------------------------- line input ------------------------------ */

#define LINE_MAX 256
#define MAX_ARGS 16

static int read_line(char *buf, size_t max) {
    size_t pos = 0;
    for (;;) {
        char c;
        ssize_t r = sys_read(0, &c, 1);
        if (r <= 0) {
            if (pos == 0) return -1;
            break;
        }
        if (c == '\r' || c == '\n') {
            put("\n");
            break;
        }
        if ((c == 0x7f || c == 8)) {
            if (pos > 0) {
                pos--;
                put("\b \b");
            }
            continue;
        }
        if (pos + 1 < max) {
            buf[pos++] = c;
            putn(&c, 1);
        }
    }
    buf[pos] = 0;
    return (int)pos;
}

static int tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

/* -------------------------------- builtins -------------------------------- */

static int cwd = ROOT;
static const char *prompt_name = "sh";

static void cmd_help(void) {
    put("available commands: ls cat cd pwd echo mkdir help clear exit bash\n");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        put(argv[i]);
        if (i + 1 < argc) put(" ");
    }
    put("\n");
}

static void cmd_pwd(void) {
    char buf[MAX_PATH];
    vfs_path(cwd, buf, sizeof(buf));
    put(buf);
    put("\n");
}

static void cmd_cd(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : "/";
    int idx = vfs_resolve(cwd, target);
    if (idx < 0) {
        put(target); put(": no such directory\n");
        return;
    }
    if (!nodes[idx].is_dir) {
        put(target); put(": not a directory\n");
        return;
    }
    cwd = idx;
}

static void cmd_ls(int argc, char **argv) {
    int idx = (argc > 1) ? vfs_resolve(cwd, argv[1]) : cwd;
    if (idx < 0) {
        put(argv[1]); put(": no such file or directory\n");
        return;
    }
    if (!nodes[idx].is_dir) {
        put(nodes[idx].name); put("\n");
        return;
    }
    for (int i = 0; i < nodes[idx].nchildren; i++) {
        int c = nodes[idx].children[i];
        put(nodes[c].name);
        if (nodes[c].is_dir) put("/");
        put("\n");
    }
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { put("usage: cat <path>\n"); return; }
    int idx = vfs_resolve(cwd, argv[1]);
    if (idx < 0) { put(argv[1]); put(": no such file\n"); return; }
    if (nodes[idx].is_dir) { put(argv[1]); put(": is a directory\n"); return; }
    if (nodes[idx].content) put(nodes[idx].content);
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { put("usage: mkdir <path>\n"); return; }
    char pathbuf[MAX_PATH];
    kstrncpy(pathbuf, argv[1], MAX_PATH);

    /* split into parent path + new name */
    char *last_slash = 0;
    for (char *p = pathbuf; *p; p++) if (*p == '/') last_slash = p;

    int parent;
    const char *newname;
    if (last_slash) {
        *last_slash = 0;
        newname = last_slash + 1;
        parent = (*pathbuf) ? vfs_resolve(cwd, pathbuf) : ROOT;
    } else {
        parent = cwd;
        newname = pathbuf;
    }

    if (!*newname) { put("mkdir: bad path\n"); return; }
    if (parent < 0 || !nodes[parent].is_dir) { put(argv[1]); put(": no such directory\n"); return; }
    for (int i = 0; i < nodes[parent].nchildren; i++) {
        if (kstreq(nodes[nodes[parent].children[i]].name, newname)) {
            put(argv[1]); put(": already exists\n");
            return;
        }
    }
    if (node_count >= MAX_NODES || nodes[parent].nchildren >= MAX_CHILDREN) {
        put("mkdir: out of space\n");
        return;
    }
    new_node(newname, 1, parent, 0);
}

static void cmd_clear(void) {
    put("\x1b[2J\x1b[H");
}

/* ---------------------------------- shell ---------------------------------- */

static void print_prompt(void) {
    char buf[MAX_PATH];
    vfs_path(cwd, buf, sizeof(buf));
    put(prompt_name);
    put(":");
    put(buf);
    put(" % ");
}

static void dispatch(int argc, char **argv) {
    const char *cmd = argv[0];
    if (kstreq(cmd, "help")) cmd_help();
    else if (kstreq(cmd, "echo")) cmd_echo(argc, argv);
    else if (kstreq(cmd, "pwd")) cmd_pwd();
    else if (kstreq(cmd, "cd")) cmd_cd(argc, argv);
    else if (kstreq(cmd, "ls")) cmd_ls(argc, argv);
    else if (kstreq(cmd, "cat")) cmd_cat(argc, argv);
    else if (kstreq(cmd, "mkdir")) cmd_mkdir(argc, argv);
    else if (kstreq(cmd, "clear")) cmd_clear();
    else if (kstreq(cmd, "exit")) sys_exit(0);
    else if (kstreq(cmd, "bash")) {
        prompt_name = "bash";
        put("bash is this same shell -- there is no separate dyld-linked bash yet.\n");
    } else {
        put(cmd);
        put(": command not found\n");
    }
}

static void banner(void) {
    put("\n");
    put("XNU/rpi3 -- freestanding PID 1 (no dyld, no libc)\n");
    put("booted via mockfs, exec'd in place of /sbin/launchd\n");
    put("type 'help' for available commands\n\n");
}

void shell_main(void) {
    open_console();
    vfs_init();
    banner();

    char line[LINE_MAX];
    char *argv[MAX_ARGS];

    for (;;) {
        print_prompt();
        int n = read_line(line, sizeof(line));
        if (n < 0) continue;
        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;
        dispatch(argc, argv);
    }
}

/* ------------------------------- entry point -------------------------------
 * The kernel's LC_UNIXTHREAD state may or may not hand us a usable sp (see
 * build.py's notes on why); never trust it -- switch to our own static
 * stack before doing anything else, including calling a real C function.
 */

#define STACK_SIZE (64 * 1024)
__attribute__((used)) static char boot_stack[STACK_SIZE];

__attribute__((naked)) void _start(void) {
    asm volatile(
        "adrp x0, _boot_stack@PAGE\n"
        "add  x0, x0, _boot_stack@PAGEOFF\n"
        "add  x0, x0, %0\n"
        "and  sp, x0, #~15\n"
        "bl   _shell_main\n"
        "mov  x0, #0\n"
        "mov  x16, #1\n"       /* SYS_exit */
        "svc  #0x80\n"
        "1: b 1b\n"
        :
        : "i"(STACK_SIZE)
        : "x0"
    );
}
