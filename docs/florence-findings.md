# Findings from running Florence on the port

Florence (<https://github.com/anurodhp/florence>) is NetSurf 3.11 with a GNUstep UI, cross-built with this
repo's toolchain recipe and run on the Pi 3 image. These are the places where the port got in its way,
written down with how sure we are of each. Observed on the device by Florence's author unless a line says
otherwise; nothing here was built or run against this repo's tree by the person writing it.

## 1. Symbols the JavaScript build needs and no dylib exports (certain: seen at link time)

Linking Florence with NetSurf's bundled Duktape failed with:

    Undefined symbols for architecture arm64:
      "_log2",    referenced from: _duk__log2 in ...duktape.o
      "_strptime", referenced from: _duk__parse_string in ...duktape.o

* `log2`: `libsystem_m.dylib` is the curated subset (`docs/webkit-browser-feasibility.md` already lists `log2`
  among the missing libm exports).
* `strptime`: no dylib exports it. `build_vim.sh` and `build_libsystemconfiguration_client.sh` work around the
  same gap by compiling Apple's `stdtime/FreeBSD/strptime.c` into the binary.

Florence's workaround (`frontend/gs_compat.c`): `log2` built from `log` (exact for powers of two), and a
`strptime` that returns NULL, which Duktape treats as "not recognised" (its Orbis configuration does the same).
Suggested here: export a real `strptime` from the owning dylib (the source is already compiled twice in this
tree) and `log2`, so consumers need no shims.

## 2. No memory-pressure notifications (certain from the source; Florence's workaround observed working)

`tools/userland_staging/dispatch_gap_closures3.c` says there is no memory-pressure delivery path (no
`NOTE_MEMORYSTATUS_*` source registration anywhere), and `memorystatus_control` / `malloc_zone_pressure_relief`
are not exported. With no swap, running out of memory is fatal, so an app has no way to be told to shrink.

Florence therefore checks after a page loads or a tab closes: `dlsym` finds `host_statistics64` and
`mach_host_self`, and `sysctlbyname("hw.memsize")` gives the total; under 15 % (free + inactive pages) of RAM
it purges its caches. On the device the start-up trace printed "low-memory checks on", so those lookups
succeed. Not checked: whether the page counts are sensible on this kernel.

Wanted: a working `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` source (or `memorystatus_control` notification), and
`malloc_zone_pressure_relief` exported, so apps can react instead of polling.

## 3. A thread beside the GNUstep run loop died with an X IO error (certain it happened; cause not found)

Florence's first EasyList updater ran libcurl on a `pthread_create` worker (512 KB stack,
`CURLOPT_NOSIGNAL`) while the main thread ran the GNUstep run loop. From that commit on, the app died within
seconds of start with Xlib's `fatal IO error 22 (Invalid argument) on X server`. Bisecting the git history
landed on exactly that commit, and the crash stopped when the same work moved into a child process
(`posix_spawn` of the same executable). With a handler installed through `XSetIOErrorHandler`, the failure was
on the main thread, inside the normal gnustep-gui event loop; errno was 22.

Not known: why. `read()` on the X socket returning EINVAL points at the socket layer or at a descriptor that
changed under Xlib, but nothing here shows which. Worth a minimal repro: a GNUstep app with a secondary
pthread running `curl_easy_perform` (or just opening and closing sockets and files).

Florence's rule for now: no threads beside the GNUstep run loop on this port.

## 4. Keep-alive failures on accepted connections (certain it is logged; reason unknown)

At start-up Florence's console fills with `failed to turn on keepalive for accepted socket N` (N between 20 and
42; about eleven within 12 ms when the window appears). The string lives in `libgnustep-base`, which logs it
when `setsockopt(SO_KEEPALIVE)` fails on a connection it accepted for distributed objects. The errno is not
printed, so it should not be assumed to be 22. Which program connects is not known; the line just before is
`Eau: No running Menu process snapshot found`, so the Eau theme's menu integration is the suspect.

Harmless on its own, but each accepted connection holds a descriptor, and the same session once reported
"Too many open files" (not diagnosed). Worth finding out why `SO_KEEPALIVE` is refused on these sockets and
who is making the connections.

## What Florence did about each

| # | Florence side | Port side |
|---|---|---|
| 1 | shim in `frontend/gs_compat.c` | export `log2`, `strptime` |
| 2 | polls on events (`frontend/gs_memory.c`) | notification delivery, `malloc_zone_pressure_relief` |
| 3 | child process instead of a thread (`frontend/gs_lists.c`) | find why a thread broke the X socket |
| 4 | none | find the connections and the `SO_KEEPALIVE` failure |
