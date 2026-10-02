#!/bin/sh
# DAR-340/H1: run as ROOT inside the booted target (host-built images cannot
# chown to root:wheel, see inject_into_sd_image.sh install_setuid_root).
# Makes su/sudo setuid root and /etc/sudoers root:wheel 0440, then prints
# `ls -ln` so the result is checkable. Idempotent. Refuses to run as non-root.
#
# It also makes the system trees root:wheel. inject_into_sd_image.sh runs as
# the host user against a `diskutil mount` without ownership, so every file
# and directory it writes lands on the HFS+ volume as uid 502 gid 20 (the
# host user's) -- /bin, /usr, /etc, /Applications, /System, /Library alike;
# chown(2) to root does not take there. Every launchd job on the image runs
# as root, so nothing on the volume depends on a non-root owner. chown -R
# without -H/-L does not follow symlinks (the /usr/GNUstep/System/*
# compatibility links are changed themselves, not walked twice). The
# setuid/sudoers steps run AFTER the bulk chown so their modes are the last
# word.
[ "$(id -u)" = 0 ] || { echo "provision-setuid: must run as root" >&2; exit 1; }
rc=0
for d in /Applications /System /Library /bin /sbin /usr /etc /var; do
    [ -d "$d" ] || continue
    chown -R root:wheel "$d" || rc=1
done
for d in / /private /Users /tmp; do
    [ -d "$d" ] && { chown root:wheel "$d" || rc=1; }
done
[ -d /tmp ] && { chmod 1777 /tmp || rc=1; }
for f in /usr/bin/su /usr/bin/sudo; do
    [ -f "$f" ] || { echo "skip: $f not installed"; continue; }
    chown root:wheel "$f" && chmod 4755 "$f" || rc=1
done
if [ -f /etc/sudoers ]; then
    chown root:wheel /etc/sudoers && chmod 0440 /etc/sudoers || rc=1
fi
# OpenPAM ignores a policy file that is not root-owned (su then fails with
# "no pam_deny.so found"): the host cannot chown it either.
if [ -f /etc/pam.d/su ]; then
    chown root:wheel /etc/pam.d/su && chmod 0644 /etc/pam.d/su || rc=1
fi
ls -ln /usr/bin/su /usr/bin/sudo /etc/sudoers /etc/pam.d/su 2>&1
ls -lnd /Applications /System /Library /usr /usr/local /etc /tmp 2>&1
exit $rc
