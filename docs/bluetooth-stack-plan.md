# Bluetooth host stack (L2CAP and up) for the Pi 3

Status (2026-11-03): classic Bluetooth works end to end on QEMU (S0-S3, S5);
real-hardware test, legacy-PIN pairing test and Secure Simple Pairing (S4)
are next. BLE (S8) is deferred.

- **S0:** fork `anurodhp/netbsd-src`, branch `iokit-port`, made from
  `netbsd-11` at `4198f5370c18` (11.1). It is a sparse checkout, pinned in
  `setup_third_party.sh`.
  - H0: an `IOHIDResource` personality, and `hid_userdevice_test`
    (`tools/hid_userdevice_qemu_test.sh`).
- **S2: netbt in the kernel.**
  - `tools/vendor_netbt_into_xnu.sh` copies `sys/netbt` unchanged to
    `bsd/netbt`, with this repo's glue (`compat/netbt/`):
    - `netbt_compat.h`: the NetBSD API on xnu, cited per mapping;
    - `netbt_xnu.c`: the domain and protosw table, callouts, softints,
      sockopts and kauth;
    - `netbt_bridge.c`: the `hci_if`.
  - The xnu fork adds `AF_BLUETOOTH` = 41 and attaches the domain in
    `domaininit`.
  - Boot-arg `netbt-debug=N` turns on netbt's own `DPRINTF`s.
- **S1: the driver is netbt HCI unit `bcmbt0`.**
  - `BCM43430Bluetooth` attaches after `bt up`.
  - Packets go to netbt once `btconfig bcmbt0 up` enables the unit.
  - TX is an interrupt-driven ring.
- **S3: NetBSD userland ported.**
  - Scripts: `build_libbluetooth.sh`, `build_netbt_tools.sh`,
    `build_libevent.sh`, `build_libprop.sh`, `build_libusbhid.sh`.
  - Commands: `btconfig`, `btpin`, `btkey`, `btdevctl`, `sdpquery`,
    `rfcomm_sppd`, `sdpd`, `bthcid`.
  - Libraries: libbluetooth, libevent, libprop, libusbhid.
  - libc gaps are filled by `compat/netbt-user/`.
  - launchd jobs: `sdpd`, `bthcid`, `bthidd`. The `bluetooth` job ends with
    `btconfig bcmbt0 up pscan`.
- **S5: `bthidd`.**
  - L2CAP control and interrupt channels, then an `IOHIDUserDevice` per
    device that `btdevctl -A` attached (its cache, `/var/db/btdevctl.plist`).
  - Output reports (keyboard LEDs) are not forwarded yet.
- **QEMU tests:** `tools/bt_h4_responder.py` now also plays a Bluetooth
  keyboard.
  - `tools/bt_netbt_qemu_test.sh`: the unit comes up, an inquiry runs through
    netbt, and a local SDP browse works.
  - `tools/bt_hid_qemu_test.sh`: the keyboard reconnects on page scan; detach
    closes both channels (DisconnReq) and the link expires; attach connects
    from the host; its "h"/"i" keys arrive at `IOHIDEventService`
    (`hid_event_smoketest` PASS).
- **Lessons:**
  - xnu calls `pru_control` with the socket (= domain) lock held, but
    NetBSD's `hci_ioctl` takes `bt_lock` itself. That deadlocked `btconfig`
    until the wrapper dropped the lock around the call.
  - The image keeps `/var/log` and `/var/db` between test runs, so stale logs
    read as fresh evidence. Tests truncate what they read.

## What exists today, and what does not

| Layer | State |
|---|---|
| Controller power, patch, baud | done (`BCM43430Bluetooth.cpp`, `bt up`) |
| HCI commands and events to userland | done, one command at a time (`kBtMethodCommand`, `kBtMethodReadEvent`) |
| ACL receive | deframed in the driver (`kBtH4Acl`), queued with events |
| ACL transmit | **missing**: no user-client method, TX is polled and sized for commands |
| HCI multiplexing (several users), ACL flow control (controller buffer credits) | **missing** |
| L2CAP, SDP, RFCOMM, pairing and link keys | **missing** |
| HID host, A2DP source | **missing** |
| BLE (ATT/GATT, SMP) | **missing** |

Other parts of the system that the profiles will need are already in place:

- **IOHIDUserDevice.** It is compiled into the kernel
  (`xnu-7195/iokit/conf/files:263-265`: `IOHIDResource`,
  `IOHIDResourceUserClient`, `IOHIDUserDevice`). The USB keyboard and mouse
  already reach X through `IOHIDEventDriver` (`BCM2837Personalities.cpp:312-345`).
- **audiod.** It drives PWM audio (`docs/audio-bringup.md`).

## Where a stack can come from

Apple never published IOBluetoothFamily or bluetoothd (the
`apple-oss-distributions/IOBluetoothFamily` lookup failed, 2026-10-12;
`docs/bluetooth-plan.md`). Following the source-priority rule, that leaves
the BSDs.

| Source | What it is | Fit |
|---|---|---|
| **NetBSD `sys/netbt`** | A plain BSD socket domain: `AF_BLUETOOTH` with `BTPROTO_HCI`, `L2CAP`, `RFCOMM` and `SCO` (`hci_*.c`, `l2cap_*.c`, `rfcomm_*.c`, `sco_*.c`, about 430 KB of source with comments). A driver attaches through one small table, `hci_attach_pcb(const struct hci_if *, ...)` (`hci_unit.c:84`). BSD licence. | **Best.** It needs no new framework. xnu already loads old-style domains (`net_add_domain_old` / `net_add_proto_old`, `bsd/kern/uipc_domain.c:256,445`). |
| NetBSD userland | `btconfig`, `bthcid` (link keys and PIN agent), `sdpd`, `sdpquery`, `btkey`, `btpin`, `rfcomm_sppd`, `bta2dpd` (A2DP source with its own **SBC encoder**, `sbc_encode.c`, and AVDTP), `btpand`, `lib/libbluetooth` (address and SDP helpers). All use `AF_BLUETOOTH` sockets, so they port unchanged once netbt exists. | Good. `bthcid` needs libevent (`#include <event.h>`); `btdevctl` needs proplib. |
| NetBSD `sys/dev/bluetooth` | `bthidev`, `btkbd`, `btms`, `btmagic` (Apple Magic Mouse), `btsco`, `btuart`. | Use the logic only. They attach to wscons, which this system does not have. |
| FreeBSD `sys/netgraph/bluetooth` | `ng_hci`, `ng_l2cap`, `ng_btsocket`, plus `hccontrol`, `bthidd` and `sdpd`. | Needs **netgraph** itself, a large port with no other use here. Read `bthidd` as a reference only. |
| OpenBSD | Imported NetBSD's netbt and later removed it. | Nothing to add. |
| Apache NimBLE | Userland BLE host: GAP, ATT/GATT, SMP. Apache-2.0, maintained (`apache/mynewt-nimble`). Ships ports for Linux, NuttX and others. | **The LE source.** It does LE only; see S8. |
| BTstack | Classic and LE. | Its licence is not an open-source licence (GitHub reports `NOASSERTION`; free for non-commercial use only). Do not use. |

**Two netbt gaps found while reading it:**

1. **No Secure Simple Pairing.** `hci_event.c` (1331 lines) and `bthcid`
   handle only legacy PIN pairing. There is no IO Capability Request, User
   Confirmation or Passkey handling.
   - Devices from Bluetooth 2.1 on must fall back to PIN pairing when the
     host has SSP turned off. Most keyboards then ask for the PIN to be
     typed on the keyboard.
   - "Secure Connections Only" devices refuse that fallback.
   - Adding SSP is S4.
2. **No LE host.** `hci_le.h` (2024) contains only constants. L2CAP has no
   LE fixed channels and there is no SMP or ATT.

## Recommendation

**Port NetBSD netbt into the xnu fork as `AF_BLUETOOTH`, and port its
userland unchanged.** `BCM43430Bluetooth` becomes a netbt `hci_if` and
keeps only power, patch and baud. The profiles go in userland:
- HID through `IOHIDUserDevice`;
- A2DP through `bta2dpd` fed by audiod.

That covers classic Bluetooth: Apple Magic Keyboard, Mouse and Trackpad,
most other keyboards, headphones, phones, and serial. BLE comes later from
NimBLE (S8).

Why kernel netbt rather than a userland stack over the current user client:
- The BSD tools work unchanged over sockets. A userland stack would need its
  own IPC, and every tool would have to be rewritten against it.
- netbt locks with a single global mutex (`bt_lock`), so it is easy to get
  right in xnu.
- The driver interface is already the shape `BCM43430Bluetooth` has: enable,
  disable, output command, output ACL, output SCO, stats.

What it costs:
- About 10k lines of kernel C, compiled against a small NetBSD-to-xnu
  compatibility header.
- A fork of `NetBSD/src` (sparse checkout of `sys/netbt`, `usr.sbin/bt*`,
  `usr.bin/bt*`, `sdpd`, `sdpquery`, `rfcomm_sppd`, `bta2dpd` and
  `lib/libbluetooth`), pinned to a release tag, as PureDarwin is pinned.

## Steps

### S0. Groundwork and the HID check (before any Bluetooth code)

- Fork `NetBSD/src` to `anurodhp/netbsd-src`, branch `iokit-port`, sparse
  checkout. Pin a release tag (choose it with `git ls-remote`) in
  `setup_third_party.sh` and `docs/repos-to-clone.md`.
- **H0, which de-risks HID.** Build IOHIDFamily's own
  `tools/IOHIDUserDeviceTest.c` (or a small equivalent) and check that a
  userland `IOHIDUserDevice` keyboard types into X and a mouse moves the
  pointer. Use the same path as the USB keyboard. If `IOHIDResource` does not
  match or the event driver does not attach, fix that first: S5 depends on it.

### S1. Driver: become a netbt `hci_if`

- Implement `enable`, `disable`, `output_cmd`, `output_acl`, `output_sco`
  (refuse it; SCO goes to the PCM pins on the Pi 3), and `stats`.
- RX: hand each deframed packet to netbt's `hci_input_event` /
  `hci_input_acl` from the worker thread. That thread already exists and is
  not at interrupt level.
- **TX becomes interrupt-driven (TXIM)**, with a queue. ACL traffic (A2DP
  especially) is far larger than the commands TX was sized for.
- Keep `up` (power, patch, baud) in the driver, and attach to netbt only after
  `up` succeeds. netbt's unit init (Reset, Read_BD_ADDR, Read_Buffer_Size,
  etc.) then runs on the patched controller at 921600.
  - netbt's own Reset must not undo the baud: an HCI Reset after the patch
    keeps the UART rate. Confirm this on the H4 responder and on hardware.
  - If the rate does reset, intercept it.
- The user client keeps `up` and `status`. The raw `command` and
  `readEvent` methods go once `btconfig` covers them (netbt's
  `BTPROTO_HCI` raw socket replaces them). `bt scan` and `bt hci` then move
  onto that socket or are retired.

### S2. netbt in the xnu fork

- `bsd/netbt/` in `third_party/xnu-7195` (branch `bcm2837-bringup`), built
  in by `bsd/conf/files`, and registered at boot with `net_add_domain_old`.
- Add `AF_BLUETOOTH` to `bsd/sys/socket.h` as the next free number (41, with
  `AF_MAX` raised to 42). NetBSD's value, 31, is xnu's `AF_NATM`. The
  userland headers must match the fork.
- Compatibility header (`netbt_compat.h`), one place for every mapping, each
  with a citation per DAR-313:
  - mbufs: the xnu kernel has the BSD `m_*` API under `BSD_KERNEL_PRIVATE`.
  - `callout` → `thread_call`.
  - `kmutex` → `lck_mtx`.
  - `pool` → `zone`.
  - `softint` → the driver's worker thread.
  - `sysctl` → xnu `SYSCTL_*`.
  - `kauth` exists in xnu.
  - NetBSD's split `pr_usrreqs` (`pr_attach`, `pr_send`, …) → xnu's
    `struct pr_usrreqs` (`pru_attach`, …).
- What netbt calls, counted in `sys/netbt` at the pin: about 60 distinct
  kernel functions.
  - mbuf: `m_copydata` 72, `m_adj` 67, `m_freem` 48.
  - Sockets: `solocked` 64; `sockopt_*` (NetBSD's `struct sockopt` helpers,
    which map onto xnu's `sooptcopyin` / `sooptcopyout`); the `sb*` and
    `so*` state calls.
  - `callout_*` 40 in all, including `callout_ack` / `callout_invoking`,
    which handle a stop racing a firing callout.
  - `sysctl_createv` 24; `mutex_*`; `pool_*`; `kauth_*`.
  - `MBUFQ_*` and `softint_*` (one per unit: `hci_intr`).
- **Locking fits xnu:** every netbt socket shares the global `bt_lock`
  (`mutex_obj_hold`). An xnu domain without `PR_PROTOLOCK` is locked by its
  one domain mutex, so `bt_lock` becomes that mutex.
- `hci_enable()` itself sends HCI Reset and waits for init (5 s). This runs
  after the driver's `up` has switched to 921600, the same order as Linux
  (operational speed, then `btbcm_initialize` → reset), so the reset should
  keep the speed. Confirm it on hardware.
- Leave out the proplib ioctls that configure kernel `bthidev` and `btsco`;
  those profiles live in userland here.
- QEMU test: `bt_h4_responder.py` extended to answer netbt's init sequence.
  A raw HCI socket reads the local BD_ADDR.

### S3. Userland: tools and libraries

Use the `build_libc_regex.sh` template for each. Every binary is dynamic and
passes `bind_target_audit.sh`.
- `lib/libbluetooth` (dylib)
- `btconfig`
- `btkey`, `btpin`
- `sdpd`, `sdpquery`
- `rfcomm_sppd`
- `bthcid`, which needs libevent: pin upstream libevent (BSD) or move it to
  kqueue. Decide when porting.

Install: `inject_into_sd_image.sh`, plus launchd jobs for `bthcid` and
`sdpd` (in jetsam band 19, as the other daemons are).
- Test on hardware: `btconfig ubt0 inquiry`; `sdpquery` against a phone;
  legacy PIN pairing with `btpin`; an RFCOMM serial link to a phone or the
  Mac with `rfcomm_sppd`.

### S4. Secure Simple Pairing

- In netbt: enable `Write_Simple_Pairing_Mode`, and handle IO Capability
  Request/Response, User Confirmation Request, User Passkey Request/Notify,
  and Simple Pairing Complete.
- In bthcid: answer them as an agent (Just Works for mice; passkey display
  for keyboards), and store link keys as it already does
  (`/var/db/bthcid.keys`).
- Later: a GUI agent, a Naples menu-bar panel like the Wi-Fi one, and a
  Turin pane for paired devices.

### S5. HID host (`bthidd`)

- A userland daemon that:
  - listens on L2CAP PSM 0x11 (control) and 0x13 (interrupt), because devices
    reconnect to the host;
  - reads the HID descriptor through SDP (attribute 0x0206);
  - creates an `IOHIDUserDevice` with that descriptor and forwards input
    reports;
  - sends output reports (keyboard LEDs) back.
- Logic from NetBSD `bthidev.c` / `btmagic.c` and FreeBSD `bthidd`. A device
  list stands in for `btdevctl`.
- Test: Apple Magic Keyboard and Mouse (classic HID), plus one third-party
  keyboard, typing in Terminal under X.

### S6. Audio: A2DP source

- Port `bta2dpd` (AVDTP plus SBC, BSD). audiod gains a Bluetooth output that
  feeds it PCM; device selection goes in Settings/Naples later.
- Bandwidth: SBC at bitpool 53 is about 43 KB/s against roughly 92 KB/s of
  raw UART at 921600, and the 3B has **no flow control in either direction**.
  - If the controller's receive side overruns, try 2000000 baud, or RTS/CTS
    on GPIO30/31 ALT3. The board's line names call those pins "CTS0/RTS0
    Used by BT module" (`docs/bluetooth-plan.md`, risk 1).
  - Stress-test with Wi-Fi traffic running, since both radios share 2.4 GHz.
- HFP and the headset microphone (SCO) are out of scope: on the Pi 3, SCO is
  routed to the PCM pins, not the UART.

### S7. Optional: PAN

`btpand` needs a tap interface, which xnu lacks. It could go through
`utun`/`feth` or stay out. It is low priority with Wi-Fi and Ethernet
present.

### S8. BLE (its own plan, later)

Many current mice and keyboards (for example Logitech's) and most sensors are
LE-only. There are two candidate routes:

- **(a)** Add LE to netbt: LE connection events, the LE-U ACL path, and
  L2CAP fixed channels 4 (ATT) and 6 (SMP). Then run NimBLE's ATT/GATT and
  SMP code in userland over those channels. HOGP (HID over GATT) goes into the
  same `IOHIDUserDevice` path as S5.
- **(b)** Run all of NimBLE in userland, with netbt passing LE connection
  handles through untouched.

(a) keeps one owner for the controller. (b) is less kernel work, but splits
connection-handle ownership. Decide after S5, and consider LE Secure
Connections crypto (P-256, AES-CMAC) when deciding.

## Testing strategy

- **QEMU:** the H4 responder (`tools/bt_h4_responder.py`) covers init, the
  patch and HCI framing. Connections and profiles need a remote device.
  - Evaluate Google's Bumble (Apache-2.0, Python) as a software controller
    and remote device behind the QEMU PL011 socket. Check its classic (BR/EDR)
    support before relying on it.
  - Otherwise, test profiles only on hardware.
- **Real Pi:** a phone (SDP, RFCOMM, pairing), Apple Magic Keyboard and Mouse
  (HID), headphones (A2DP). Keep the overrun and framing counters at 0, and
  run Wi-Fi traffic at the same time.

## Risks

1. **The netbt kernel port.** NetBSD and xnu socket and mbuf semantics differ
   in small ways (`sbappend` variants, `soisconnected`, mbuf flags). Port one
   protocol at a time (HCI raw socket, then L2CAP, then RFCOMM), each tested
   before the next.
2. **No UART flow control** (see S6). A2DP is the first real bandwidth user.
3. **IOHIDUserDevice untested here** (H0 settles this first).
4. **Pairing UX.** Without a GUI agent, pairing needs a root shell (`btpin`
   or a bthcid prompt).
5. **AF number drift.** `AF_BLUETOOTH` = 41 is ours. A later SDK that takes
   41 would collide, so keep the value in one header shared by the kernel and
   userland.

## Rough size

| Step | Main work |
|---|---|
| S0 + H0 | fork, pin, HID check: small |
| S1 | driver TX and `hci_if`: medium |
| S2 | netbt kernel port: large (the bulk) |
| S3 | userland ports: medium, mechanical |
| S4 | SSP: medium |
| S5 | `bthidd`: medium |
| S6 | `bta2dpd` + audiod: medium, with bandwidth risk |
| S8 | BLE: large, own plan |
