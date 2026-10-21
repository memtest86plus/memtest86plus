# mtqemu.py — QEMU test harness for memtest86+

`mtqemu.py` runs memtest86+ under QEMU headlessly by default: it builds the
chosen port, stages boot media, boots the guest, watches the screen via QMP
screendumps and either reports sustained test activity (smoke mode), sets up
a GDB debugging session (debug mode), or boots the guest in a visible GUI
window and lets you interact with it (run mode).

    python3 tools/mtqemu.py                     # smoke on x86_64, UEFI + ESP
    python3 tools/mtqemu.py -a i586             # smoke on i586, floppy (BIOS)
    python3 tools/mtqemu.py -a aarch64          # smoke on aarch64
    python3 tools/mtqemu.py -a loongarch64      # smoke on la64 (xfail, see below)
    python3 tools/mtqemu.py debug               # GDB session (UEFI, port 1234)
    python3 tools/mtqemu.py run                 # interactive GUI window
    python3 tools/mtqemu.py run --keys c,c --key-delay 2,1

All four ports (i586, x86_64, aarch64, loongarch64) are supported. Only
python3 (no third-party modules) plus the external tools listed below are
required. TCG mode is used by default (`-accel tcg,split-wx=on`);
`--accel VALUE` overrides it (for example `--accel tcg` or a KVM value).

## Modes

### smoke (default)

Builds `build/<arch>/mt86plus` if missing, stages boot media and boots the
guest for `--time` seconds (default 180). Screendumps are taken every
`--dump-interval` seconds (default 1.0). `--keys` (see [run](#run)) can
drive scripted interactions — in smoke mode the qcodes are sent at 25% of
the runtime. The result is decided by a frame progress detector:

* *first paint*: the first parsed frame that is not all black and has at
  least 3 unique colors (SeaBIOS text POST = 2 colors; the memtest UI is
  blue-background text or a 640x480 GOP screen).
* after a short settle window, the detector requires *sustained activity*:
  enough of the recent screendumps must differ from their predecessor
  (a >2 px change counts; resolution switches are changes as a whole).
* verdicts: `pass` (sustained activity; early exit as soon as the criterion
  is met), `no_progress` (screen frozen after paint, >20 dumps), or
  `boot_failure` (never painted).

On pass the temporary work directory (with all dumps) is deleted unless
`--keep-dumps` or `--output-dir` are given. On failure everything is kept
and the path is printed; `result.json` and `serial.log` are always written
in that case. `--json` prints the machine-readable summary:

```json
{"arch": "...", "bootmode": "...", "first_paint_s": 4.2, "activity_ratio": 0.93,
 "changed_px_timeline": [...], "qemu_exit": 0, "status": "pass", "xfail": false,
 "rationale": "...", "artifacts": [...]}
```

### debug

Sets up a GDB debugging session in QEMU, replacing the former
`build/x86_64/debug_memtest.sh` and `build/i586/debug_memtest.sh`;
the workflow and generated `gdbscript` are identical:

1. `sed '/DISCARD/d' ldscripts/memtest_shared.lds >
   build/<arch>/memtest_shared_debug.lds`
2. `make debug DEBUG=1` (creates the `memtest.debug` symbol file)
3. generate `build/<arch>/gdbscript` **only if it does not exist yet** — a
   hand-written `gdbscript` is honored unchanged; delete it to get a fresh
   one with recomputed offsets
4. stage `build/<arch>/hda-contents/` (x86 UEFI) or an ESP image
   (aarch64/la64) with the freshly built mt86plus
5. start QEMU with `-gdb tcp::PORT -S`, then run GDB in a terminal
   (auto-detected: `x-terminal-emulator -e`, `gnome-terminal --`,
   `xterm -e`; override with `--terminal-cmd`). With none of the three
   available, GDB is started directly in the caller's terminal (useful for
   headless/ssh workflows); QEMU is stopped after GDB exits.

The generated script loads symbols at `IMAGE_BASE+0x1000` (the preferred
load address taken from `boot/*/header.S`) and, for x86, at the relocated
position `0x400000`, with the `.data`/`.bss` addresses taken from
`objdump -t memtest.debug`. QMP, serial logging
(`build/<arch>/debug_serial.log`) and screendumps remain available while
you debug.

Debug specifics per port:

| port        | firmware / media                       | notes |
|-------------|----------------------------------------|-------|
| x86_64      | OVMF (4M pair preferred) + `fat:rw:hda-contents`, legacy `OVMF.fd` via `-bios` too | default; `--bootmode floppy` debugs the BIOS path (symbols match after the image reaches its load location) |
| i586        | OVMF32 pair (build dir or `/usr/share/OVMF/OVMF32_*.fd`) | UEFI mode errors clearly if ia32 OVMF is not installed; use `--bootmode floppy` otherwise |
| aarch64     | AAVMF 64MB pair + virtio ESP image     | experimental (single-`add-symbol-file` at the preferred base) |
| loongarch64 | qemu-efi-loongarch64 pair + NVMe ESP   | experimental (as above) |

`--gdb PORT` sets the gdb stub port (default 1234) and is written into a
freshly generated gdbscript; with an existing script the old port 1234
still applies.

`--clean` mirrors the old script's `-c`: it deletes `hda-contents`,
`gdbscript`, firmware copies, `memtest_shared_debug.lds`, `qmp.sock`,
`debug_serial.log`, `esp_debug.img` and runs `make clean` in the build
directory. Save your own `gdbscript` and firmware copies first!

For the GDB workflow inside UEFI (boot-order via
`bcfg boot add 0 ...`, auto-boot with optional data) see
`doc/HOW_TO_DEBUG_WITH_GDB.md`.

### run

Interactive mode with a visible GUI window (`-display gtk`, override with
`--display`, e.g. `none` for scripting). The guest keeps running until you
close the window or press Ctrl-C in the launching terminal; `--time` becomes
an optional cap instead of a runtime. As soon as the guest has painted
anything, the exit status is 0; ending without any paint is `boot_failure`
(2), an interrupted session is 130. QMP screendumps and the serial log stay
available while you interact, and all verdict progress is printed live.

`--keys c,c` sends comma-separated QEMU *qcodes* to the guest, starting
`--key-delay INITIAL,BETWEEN` seconds after the first paint (default
`1.5,0.5`); repeats use the syntax `key*COUNT` (e.g. `c*3` = three `c`).
Use `--display none` to rehearse and verify an interactive run in a
headless session:

    python3 tools/mtqemu.py run --display none -t 40 --keys c,c --key-delay 2,1

## Options

| option | meaning |
|--------|---------|
| `-a, --arch` | `i586`, `x86_64` (default), `aarch64`, `loongarch64` |
| `--bootmode` | `esp` (UEFI disk image), `floppy` (BIOS image, x86 only), `kernel` (`-kernel/-append`, x86 only), `iso` (make + boot the El Torito ISO), `auto` (per-arch default: x86_64 esp, i586 floppy, aarch64/loongarch64 esp) |
| `--machine`, `--cpu` | QEMU machine/CPU overrides (defaults: `pc`/native i586, `q35`/native x86_64, `virt`/`cortex-a72`, `virt`/`la464`) |
| `--smp M,N,P` | unconditional triple: `M` cores per socket, `N` sockets, `P` NUMA nodes (must divide `M*N`); `P=1` emits no NUMA options, `P>1` emits per-node `-object memory-backend-ram,...,prealloc=on` + `-numa node,...` pairs (same style as upstream commit 507f7c4e5bc1) |
| `--ram MB` | guest RAM (default 1024, 2048 on x86_64) |
| `--cmdline` | kernel command line (`-kernel/-append`; `--bootmode kernel`, or the x86 BIOS/floppy path) |
| `--accel VALUE` | QEMU `-accel` value (default `tcg,split-wx=on`) |
| `--image PATH` | `--bootmode kernel` image (default `build/<arch>/mt86plus`) |
| `--no-reboot` | add `-no-reboot` |
| `--nic NAME` | add a user-mode NIC, repeatable: `e1000`, `e1000e`, `igb`, `rtl8139`; `e1000`/`e1000e`/`igb` select `q35` automatically |
| `--usb-msd PATH` | attach a raw disk image or block device as a writable USB mass storage device, repeatable up to 4; uses the default `qemu-xhci` controller |
| `--usb-host SPEC` | attach a physical USB device, repeatable: `VID:PID`, `BUS.PORT`, or QEMU `key=value` properties; uses the default `qemu-xhci` controller |
| `--pci-host BDF` | attach a host PCI device via VFIO, repeatable; x86_64 only; selects `q35` by default |
| `--time`, `-t SEC` | runtime cap in seconds (smoke: default 180; run: optional cap, unbounded otherwise) |
| `--display BACKEND` | QEMU display backend (default: `gtk` in run mode, `none` otherwise) |
| `--dump-interval` | seconds between screendumps |
| `--keep-dumps` / `--output-dir DIR` | keep artifacts even on pass |
| `--json` | machine-readable summary on stdout |
| `--gdb [PORT]`, `--gdb-cmd BIN`, `--terminal-cmd CMD` | debug mode options |
| `--keys KEYS` | comma-separated QEMU qcodes (repeats `key*COUNT`); smoke: at 25% of the runtime, run: after first paint |
| `--key-delay INITIAL,BETWEEN` | run mode: seconds before the first key after first paint and between keys (default `1.5,0.5`) |
| `--raw-qemu "…"` | tokens appended to the QEMU command line (escape hatch for `-accel`, `-numa dist`, `-acpitable`, ...); works in smoke, run *and* debug mode — see [Extra devices](#extra-devices-for-feature-branch-development) |
| `--clean` | debug mode: remove all debug artifacts (`make clean` included) |
| `--dump-qemu-cmd` | print the exact QEMU command line and exit |

`--dump-qemu-cmd` is useful to replay a failed boot by hand.

## Extra devices for feature-branch development

Development can require extra virtual and/or real hardware: for instance USB
host controllers/devices, NICs, and a way to watch the result on the host.
USB MSD images/block devices, physical USB devices and host PCI devices
have first-class `--usb-msd`, `--usb-host` and `--pci-host` options;
everything else is passed straight to QEMU through `--raw-qemu "…"`: the
tokens are placed at the very end of the generated command line, in every
mode — in debug mode after `-gdb`/`-S`. Debug mode runs with an extra
`-net none`, which coexists fine with `-netdev`/`-device` pairs appended
afterwards (QEMU only warns that the netdev has no peer until a device is
attached to it).
NOTE: the tap/bridge, physical passthrough and USB-serial cases require extra
privileges and/or hardware. PCI passthrough is currently x86_64-only.

Guest-side device coverage for those branches (what can be emulated vs. what
needs physical hardware):

| branch     | guest driver    | QEMU device                                          | usable in QEMU? |
|------------|-----------------|------------------------------------------------------|-----------------|
| networking | Intel 82540/82545EM | `e1000` (defaults to 82540EM), `e1000-82545em`   | yes |
| networking | Intel 82574/82576   | `e1000e` (82574), `igb` (82576)                  | yes |
| networking | Realtek 8139 (legacy mode) | `rtl8139`                                 | yes |
| networking | Realtek 8168/8169 (modern) | passthrough (`--pci-host`)               | physical device |
| networking | Atlantic ATL1E | passthrough (`--pci-host`)                         | physical device |
| networking | Broadcom TG3 | passthrough (`--pci-host`)                           | physical device |
| usbserial  | CH341 `1a86:7523`   | passthrough (`--usb-host`)                       | physical device |
| usbserial  | CP210x `10c4:ea60`  | passthrough (`--usb-host`)                       | physical device |
| usbserial  | PL2303 `067b:2303`  | passthrough (`--usb-host`)                       | physical device |

USB notes up front:

* All ports ship a `qemu-xhci` controller by default. Do not add another
  `-device qemu-xhci` in `--raw-qemu` unless you deliberately want a second
  controller; `--usb-msd` and `--usb-host` reuse the default one.
* `--usb-msd` and `--usb-host` share the default controller's four USB2
  ports. aarch64/loongarch64 also put `usb-kbd` on that controller, leaving
  fewer ports; add a second controller and attach devices explicitly
  through `--raw-qemu` if needed.
* `--usb-host` accepts `VID:PID` (e.g. `1a86:7523`), `BUS.PORT`
  (e.g. `3.2.1`), or QEMU `key=value` properties. Prefer `BUS.PORT` for
  stable automation. Supported property keys are `vendorid`, `productid`,
  `hostbus`/`hostport`, `hostaddr`, `hostdevice`, `serial`, `guest-reset`
  and `guest-resets-all`; use `--raw-qemu` for anything else.
* A missing `--usb-host` device only produces a warning; QEMU leaves the
  device empty, which keeps CI runs usable without hardware.
* Give `netdev`/`drive` ids that do not collide with the harness' own
  `id=` names (`nv0` for the loongarch64 NVMe ESP, `usb0` for ISO boot on
  non-x86 ports, `usbmsd0`..`usbmsd3` for `--usb-msd`).
* QEMU's *emulated* `-device usb-serial` will not work with the `usbserial`
  branch — its guest drivers target real adapters (CH341/CP210x/PL2303), so
  use `--usb-host` passthrough instead.

### NIC reached from the local network

User-mode networking (slirp): the guest behaves like a NAT client behind the
host — it gets `10.0.2.15/24` plus gateway/DNS via DHCP, and outgoing
connections go out through the host:

    python3 tools/mtqemu.py -a x86_64 --nic e1000
    python3 tools/mtqemu.py -a x86_64 --nic e1000e --nic e1000e   # two ports, q35

For DHCP traffic toward the *local network* rather than slirp's own DHCP,
or for a real LAN segment, use a tap attached to a bridge (needs root
privileges or `CAP_NET_ADMIN`, and the `qemu-bridge-helper` binary):

    ip link add br0 type bridge      # host-side privileged setup, then:
    python3 tools/mtqemu.py -a x86_64 --raw-qemu "-device e1000,netdev=n0 -netdev tap,id=n0,ifname=tap0,br=br0,helper=/usr/lib/qemu/qemu-bridge-helper"

### Packet capture on a virtual network

QEMU can dump every frame crossing a netdev into a host-side pcap with
`-object filter-dump` — no root, no bridge needed; open it later with
`wireshark`/`tcpdump`:

    python3 tools/mtqemu.py -a x86_64 --raw-qemu "-netdev user,id=n0,hostfwd=tcp::16645-:4272 -device e1000,netdev=n0 -object filter-dump,id=c0,netdev=n0,file=/tmp/opencode/capture.pcap"
    nc -q1 127.0.0.1 16645                 # from the host: triggers slirp ARP frames
    wireshark /tmp/opencode/capture.pcap

An inbound `hostfwd` connection is an easy way to *generate* frames even
when the guest itself never transmits (slirp then ARPs for the guest IP).
Between two VMs or against a host-side peer process, use `-netdev
socket,...`.

### USB MSD from an image or block device (exFAT tests)

Create a raw backing file and present it as a writable USB mass storage
device; memtest86+ sees an ordinary USB drive to save reports on:

    truncate -s 256M /tmp/opencode/usb-msd.img && mkfs.exfat /tmp/opencode/usb-msd.img
    python3 tools/mtqemu.py -a x86_64 --usb-msd /tmp/opencode/usb-msd.img

A Linux block device works too, provided QEMU can open it for reading and
writing:

    python3 tools/mtqemu.py -a x86_64 --usb-msd /dev/sdX

`--usb-msd` is repeatable up to 4 devices, matching the default xHCI
controller's four USB2 ports. memtest currently uses only the first mass
storage device it finds, so attach one per boot for now. Images and block
devices are always attached raw and writable, with `removable=on`. Use
`--raw-qemu` for qcow2, read-only, snapshot, or other custom block
configurations.

The block device must not be mounted or otherwise in use, and the guest gets
raw write access to it. Use a dedicated device, double-check the path, and
prefer unprivileged QEMU with suitable udev/group permissions over running
the whole tool as root.

### Physical USB MSD passthrough

Attach one of the host's USB mass storage devices with `--usb-host`.
Look up the IDs with `lsusb` and the bus/port with `lsusb -t`, then either
match by ID or pin a physical port (`BUS.PORT`, preferred once devices
wiggle around):

    lsusb                                  # e.g. "ID 0dd8:0510 Martek Corp..."
    lsusb -t                               # bus/port topology
    python3 tools/mtqemu.py -a x86_64 --usb-host 0dd8:0510
    python3 tools/mtqemu.py -a x86_64 --usb-host 3.2.1

The equivalent property-list forms are useful when QEMU needs extra
options:

    python3 tools/mtqemu.py -a x86_64 --usb-host vendorid=0x0dd8,productid=0x0510
    python3 tools/mtqemu.py -a x86_64 --usb-host hostbus=3,hostport=2.1

The device node under `/dev/bus/usb` must be writable by the user running
QEMU (via a `udev` rule or group membership — prefer that over running QEMU
itself as root). Example rule:

    SUBSYSTEM=="usb", ATTR{idVendor}=="0dd8", ATTR{idProduct}=="0510", MODE="0660", GROUP="plugdev"

Images created by the passthrough are the real device's content; the guest
can write a report just like it would on bare metal.

### USB serial adapters

Same `usb-host` mechanism, pointing at the adapter: the guest sees the
physical device and the memtest86+ host-side driver (CH341 / CP210x /
PL2303) drives it itself, so no host character device (`/dev/ttyUSB0`) is
involved. Example with the common CH340/CH341 adapter:

    lsusb                                  # find adapter IDs
    python3 tools/mtqemu.py -a x86_64 --usb-host 1a86:7523

The host kernel driver (e.g. `ch341`) is detached automatically when
libusb claims the interface; if a desktop daemon (ModemManager) grabs the
adapter between your test runs, configure it to ignore the specific
product.

The current `usbserial` branches are output-only: there is no USB serial
receive path. For machine-readable output, boot with `log=usb`; for the
VT100 console, use `console=usb`. On x86, combine this with the kernel boot
path, for example:

    python3 tools/mtqemu.py -a x86_64 --bootmode kernel \
      --usb-host 1a86:7523 --cmdline "log=usb"

QEMU's `-serial file:` is the emulated 16550 and is unrelated to the USB
adapter. To automate capture, connect the guest adapter's TX/GND to a second
USB serial adapter on the host (921600 8N1; CH341 is configured for
923077), then read the host adapter with e.g. `socat`/`stty` + `cat`. This
is a manual hardware setup; no helper script is provided.

### PCI passthrough (VFIO, x86_64 only)

`--pci-host` attaches a host PCI device through VFIO. This is primarily for
testing PCIe NIC drivers that QEMU does not emulate (modern Realtek, ATL1E,
TG3, ...). It requires host IOMMU support and a device bound to `vfio-pci`;
mtqemu validates those conditions but never binds or unbinds devices for
you.

    lspci -Dnn
    python3 tools/mtqemu.py -a x86_64 --pci-host 0000:07:00.0

Host prerequisites:

* Enable the IOMMU (`intel_iommu=on iommu=pt` or `amd_iommu=on iommu=pt`)
  and reboot. Verify with `ls /sys/kernel/iommu_groups`.
* Load `vfio-pci`, unbind the host driver, and bind the device. For example:

      echo 0000:07:00.0 | sudo tee /sys/bus/pci/devices/0000:07:00.0/driver/unbind
      echo vfio-pci | sudo tee /sys/bus/pci/devices/0000:07:00.0/driver_override
      echo 0000:07:00.0 | sudo tee /sys/bus/pci/drivers/vfio-pci/bind

* Every device in the same IOMMU group must be bound to `vfio-pci` or left
  unbound. Check the group before assigning it.
* The user running QEMU needs access to `/dev/vfio/vfio` and
  `/dev/vfio/<group>` (a `udev` rule plus a group is preferred over root).
  Example rule:

      SUBSYSTEM=="vfio", GROUP="kvm", MODE="0660"

mtqemu defaults to `q35` for PCI passthrough. It does not add
`-device intel-iommu`; that is a guest vIOMMU and is unrelated to host VFIO.
The option is currently restricted to `-a x86_64`.

The host BDF and the guest BDF are different. Boot once with
`netconsole=list` to display the guest-visible BDFs, then use the selected
guest BDF in the real target:

    netconsole=list
    # screen shows e.g. pci:0000:00:04.0 ...
    netconsole=log,6665@192.0.2.2/pci:0000:00:04.0,6666@192.0.2.1

VFIO traffic bypasses QEMU, so `-netdev socket` and `-object filter-dump`
cannot observe it. For automation, connect the passed-through NIC port to a
physical peer (a second port on the same NIC, a host NIC, an isolated switch,
or another machine) and capture/respond on that peer. QEMU's network
backends cannot bridge VFIO traffic to a socket. This is a manual hardware
setup; no helper script is provided.

VFIO under the default TCG accelerator is hardware-dependent. If it fails on
your machine, try `--accel kvm` and report the difference.

### Full example: all pieces at once, also in debug mode

    python3 tools/mtqemu.py -a i586 --bootmode floppy \
      --usb-msd /tmp/opencode/usb-msd.img --usb-host 1a86:7523 --raw-qemu \
      "-netdev user,id=n0 -device e1000,netdev=n0 -object filter-dump,id=c0,netdev=n0,file=/tmp/opencode/capture.pcap"

The same `--raw-qemu` string can be appended to a `debug` invocation — the
tokens are placed after `-gdb`/`-S`, giving an interactive session with the
extra devices already attached:

    python3 tools/mtqemu.py debug -a x86_64 --raw-qemu "-netdev user,id=n0 -device e1000,netdev=n0"

## Exit codes

| code | meaning |
|------|---------|
| 0 | pass / xfail-pass |
| 1 | usage error or missing precondition (missing tool/firmware) |
| 2 | boot failure (guest never painted a screen) |
| 3 | no progress (frozen after paint) |
| 4 | build failure |
| 130 | interrupted |

## Known port limitations

* **PCI passthrough**: `--pci-host` is x86_64-only and requires host IOMMU
  plus `vfio-pci`; it is not exercised by CI.
* **USB passthrough**: `--usb-host` is available on all ports with the
  default xHCI, but only x86_64 is formally validated.
* **loongarch64**: boots to the memtest UI, then the framebuffer freezes
  (CPU stays alive; `system/io.h` LA `__outb/__inb` uses the Loongson
  IO_PROT region which QEMU's `la464` virt machine does not emulate).
  mtqemu reports status `pass` with `"xfail": true` for this case.
* **aarch64**: `--smp` with more than one vCPU currently freezes right
  after the first paint (upstream QEMU/port SMP fragility), so leave
  `--smp` at the default `1,1,1` for aarch64.
* **i586 UEFI**: needs an ia32 OVMF firmware which most distro packages do
  not carry; mtqemu errors clearly in this case — BIOS/floppy mode is the
  validated default for i586.

## External dependencies

`qemu-system-<arch>`, posix tools checked before anything heavy happens:
`mkdosfs` + `mtools` (`mmd`/`mcopy`) for ESP staging on the host, `xorriso`
only for `--bootmode iso`, `gdb` (or `gdb-multiarch` / cross-gdb for the
ARM ports) in debug mode. Missing pieces are reported with the distro
package hint instead of being silently ignored. UEFI firmwares are looked
up in `/usr/share`:

* x86_64: `/usr/share/OVMF/OVMF_CODE_4M.fd` + `OVMF_VARS_4M.fd`
  (or non-4M names, or legacy `/usr/share/ovmf/OVMF.fd`)
* i586: `/usr/share/OVMF/OVMF32_CODE.fd` + `OVMF32_VARS.fd`
* aarch64: `/usr/share/AAVMF/AAVMF_CODE.{no-secboot.,}fd` + `AAVMF_VARS.fd`
* loongarch64: `/usr/share/qemu-efi-loongarch64/QEMU_EFI.fd` + `QEMU_VARS.fd`

All work directories are created under the system temp directory (`/tmp`
respecting `TMPDIR`) and are removed when a smoke/run run passes; failures
keep the dumps, serial log and `result.json` for triage.
