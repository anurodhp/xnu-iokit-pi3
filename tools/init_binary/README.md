# init_binary -- real /sbin/launchd SD-image installer

`init.c` (the original freestanding, mockfs-booted custom PID-1 binary
from this project's early bootstrap phase) was retired 2026-08-31 --
real, dynamically-linked `launchd.macho`
(`tools/userland_staging/build_launchd.sh`) has been PID 1 since Phase
6.8 Milestone 4's "outright replace" decision, and `init.c` was never
re-added as `/sbin/launchd` afterward (confirmed: nothing in the current
boot path execs it). Its host-side tests (`host_test.c`, `_pty_test.py`,
`_pty_test_phase5.py`) and its own `build.py`/`run_qemu.sh` were removed
with it -- see `docs/static-linking-cleanup-plan.md` and Linear DAR-166/
DAR-106/DAR-137 for the retirement record.

What's left in this directory is **`inject_into_sd_image.sh`**, which is
unrelated to `init.c` and remains real, load-bearing infrastructure: it
writes the real `launchd.macho`/`pwtool.macho` (plus `/etc` contents)
into a real HFS+ SD card image's root partition, and is called by
`runvm.sh`, `tools/make_sd_image.sh`, `tools/make_realhw_sd_image.sh`,
and every userland build/verification pass in this project. See that
script's own header comment for usage.
