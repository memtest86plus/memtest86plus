#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 Regina König <koenig_regina@arcor.de> (the debug_memtest.sh ancestor)
# Copyright (C) 2022-2026 Lionel Debroux

"""
mtqemu.py - headless QEMU boot-test / debug / run harness for memtest86+.

Modes:
  smoke   build & boot mt86plus under QEMU, verify sustained screen progress.
  debug   gdb debugging session in QEMU (replaces build/*/debug_memtest.sh).
  run     interactive boot with a visible GUI; optional key injection.

Examples:
  python3 tools/mtqemu.py                      smoke on x86_64 (UEFI ESP, q35)
  python3 tools/mtqemu.py -a i586              smoke on i586 (floppy, BIOS)
  python3 tools/mtqemu.py -a x86_64 --bootmode floppy
  python3 tools/mtqemu.py -a aarch64           smoke on aarch64 (ESP via virtio)
  python3 tools/mtqemu.py -a loongarch64       smoke on la64 (static = xfail)
  python3 tools/mtqemu.py -a x86_64 --smp 4,2,4 --ram 2048
  python3 tools/mtqemu.py -a x86_64 --usb-msd /tmp/opencode/usb-msd.img
  python3 tools/mtqemu.py -a x86_64 --usb-host 1a86:7523
  python3 tools/mtqemu.py -a x86_64 --pci-host 0000:07:00.0
  python3 tools/mtqemu.py debug                gdb session (UEFI, port 1234)
  python3 tools/mtqemu.py debug -a x86_64 --bootmode floppy --gdb-cmd gdb
  python3 tools/mtqemu.py run                  interactive session (GUI window)
  python3 tools/mtqemu.py run --display none -t 20 --keys c,c --key-delay 1,0.5

Exit codes: 0 pass/xfail, 1 usage/precondition, 2 boot failure,
            3 no progress, 4 build failure, 130 interrupted.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

# ---------------------------------------------------------------------------
# tuning knobs of the smoke detector (validated against live QEMU runs)
ACTIVITY_MIN_PX = 2        # diff above this = frame "changed"
PAINT_MIN_COLORS = 3       # unique colors for a "meaningful paint"
                           # (SeaBIOS text POST = 2; memtest GOP UI = 3:
                           #  0000aa bg / black gap / aaaaaa text)
WINDOW_R = 8               # size of the sliding recent-diffs window
WINDOW_K = 3               # recent window must contain >= K active dumps
FREEZE_F = 20              # consecutive unchanging dumps => frozen
SETTLE_S = 5.0             # ignore activity this many seconds after first
                           # paint (paint transitions themselves must not
                           # count as "sustained activity")
KEYS_AT_FRAC = 0.25        # send first key at this fraction of the runtime
QMP_WAIT_S = 20            # how long to wait for the QMP socket
RELOCADDR = 0x400000       # x86 reloc load address (LOW_LOAD_LIMIT in app/main.c)
BASE_OF_CODE = 0x1000
MAX_USB_MSD = 4             # one qemu-xhci controller with four USB2 ports

USB_HOST_KEYS = {
    'vendorid', 'productid', 'hostbus', 'hostport', 'hostaddr',
    'hostdevice', 'serial', 'guest-reset', 'guest-resets-all',
}
USB_HOST_SELECTORS = {
    'vendorid', 'productid', 'hostbus', 'hostport', 'hostaddr',
    'hostdevice', 'serial',
}
PCI_BDF_RE = re.compile(
    r'^(?:([0-9a-fA-F]{1,4}):)?([0-9a-fA-F]{2}):([0-9a-fA-F]{2})\.([0-7])$')

EXIT_OK = 0
EXIT_USAGE = 1
EXIT_BOOTFAIL = 2
EXIT_NOPROGRESS = 3
EXIT_BUILDFAIL = 4

TCG = ['-accel', 'tcg,split-wx=on']
DEFAULT_ACCEL = 'tcg,split-wx=on'

# Shared NIC table used by --nic and by tests/net/qemu_net_test.py.
# machine is the preferred QEMU machine for the device.
NICS = {
    'e1000':   {'device': 'e1000',   'machine': 'q35'},
    'e1000e':  {'device': 'e1000e',  'machine': 'q35'},
    'igb':     {'device': 'igb',     'machine': 'q35'},
    'rtl8139': {'device': 'rtl8139', 'machine': 'pc'},
}

# session directive: keep all scratch work under /tmp/opencode when it exists
if os.path.isdir('/tmp/opencode'):
    tempfile.tempdir = '/tmp/opencode'

# ---------------------------------------------------------------------------
# per-architecture machine table
ARCHS = {
    'i586': {
        'qemu': 'qemu-system-i386',
        'machine': 'pc',
        'cpu': None,
        'ram': 1024,
        'bootmode': 'floppy',
        'efi_name': 'BOOTIA32.EFI',
        'devices': ['qemu-xhci'],
        'attach': 'virtio',
        'uefi': {'dirs': ['.', '/usr/share/OVMF'],
                 'code': ['OVMF32_CODE.fd'],
                 'vars': ['OVMF32_VARS.fd'],
                 'hint': "UEFI mode for i586 needs ia32 OVMF firmware (package 'ovmf-ia32', not packaged everywhere); "
                         "or copy OVMF32_CODE.fd/OVMF32_VARS.fd into build/i586/. Use --bootmode floppy for BIOS boot."},
        'dual_text': False,
        'gdbcands': ['gdb'],
        'header': 'boot/x86/header.S',
        'bzimage': True,
    },
    'x86_64': {
        'qemu': 'qemu-system-x86_64',
        'machine': 'q35',
        'cpu': None,
        'ram': 2048,
        'bootmode': 'esp',
        'efi_name': 'BOOTX64.EFI',
        'devices': ['qemu-xhci'],
        'attach': 'virtio',
        'uefi': {'dirs': ['.', '/usr/share/OVMF'],
                 'code': ['OVMF_CODE_4M.fd', 'OVMF_CODE.fd'],
                 'vars': ['OVMF_VARS_4M.fd', 'OVMF_VARS.fd'],
                 'hint': "install the 'ovmf' package or copy OVMF_CODE.fd/OVMF_VARS.fd into build/x86_64/"},
        'legacy_bios_fw': [('.', 'OVMF.fd'),
                           ('/usr/share/ovmf', 'OVMF.fd'),
                           ('/usr/share/qemu', 'OVMF.fd')],
        'dual_text': True,
        'gdbcands': ['gdb'],
        'header': 'boot/x86/header.S',
        'bzimage': True,
    },
    'aarch64': {
        'qemu': 'qemu-system-aarch64',
        'machine': 'virt',
        'cpu': 'cortex-a72',
        'ram': 1024,
        'bootmode': 'esp',
        'efi_name': 'BOOTAA64.EFI',
        'devices': ['ramfb', 'qemu-xhci', 'usb-kbd'],
        'attach': 'virtio',
        'uefi': {'dirs': ['/usr/share/AAVMF'],
                 'code': ['AAVMF_CODE.no-secboot.fd', 'AAVMF_CODE.fd'],
                 'vars': ['AAVMF_VARS.fd'],
                 'hint': "install the 'qemu-efi-arm' package (AAVMF*)"},
        'dual_text': False,
        'gdbcands': ['gdb-multiarch', 'aarch64-linux-gnu-gdb', 'gdb'],
        'header': 'boot/aarch64/header.S',
        'bzimage': False,
    },
    'loongarch64': {
        'qemu': 'qemu-system-loongarch64',
        'machine': 'virt',
        'cpu': 'la464',
        'ram': 1024,
        'bootmode': 'esp',
        'efi_name': 'BOOTLOONGARCH64.EFI',
        'devices': ['ramfb', 'qemu-xhci', 'usb-kbd'],
        'attach': 'nvme',
        'uefi': {'dirs': ['/usr/share/qemu-efi-loongarch64'],
                 'code': ['QEMU_EFI.fd'],
                 'vars': ['QEMU_VARS.fd'],
                 'hint': "install the 'qemu-efi-loongarch64' package"},
        'dual_text': False,
        'xfail': True,
        'gdbcands': ['gdb-multiarch', 'loongarch64-linux-gnu-gdb', 'gdb'],
        'header': 'boot/loongarch/header.S',
        'bzimage': False,
    },
}

# terminals tried by debug mode, in order
TERMINALS = [
    ('x-terminal-emulator', 'x-terminal-emulator -e '),
    ('gnome-terminal', 'gnome-terminal -- '),
    ('xterm', 'xterm -e '),
]


class Fail(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code
        self.msg = msg


def info(msg):
    print(msg, flush=True)


def die(code, msg):
    raise Fail(code, msg)


def find_tool(names, extra_paths=()):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    for d in extra_paths:
        for n in names:
            p = os.path.join(d, n)
            if os.path.isfile(p) and os.access(p, os.X_OK):
                return p
    return None


def find_file(dirs, names):
    for d in dirs:
        for n in names:
            if d == '.':
                p = n
            else:
                p = os.path.join(d, n)
            if os.path.isfile(p):
                return p
    return None


def run_tool(cmd, cwd=None, quiet=False):
    r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0 and not quiet:
        print(r.stdout.decode(errors='replace'))
    return r.returncode


# ---------------------------------------------------------------------------
# argument parsing / configuration

def parse_args(argv):
    p = argparse.ArgumentParser(
        prog='mtqemu.py',
        description='QEMU boot-test / debug / run harness for memtest86+.',
        epilog='exit codes: 0 pass/xfail, 1 usage, 2 boot failure, 3 no progress, 4 build failure')
    p.add_argument('mode', nargs='?', default='smoke', choices=['smoke', 'debug', 'run'],
                   help='smoke = headless boot + progress check (default), debug = gdb debugging session, run = interactive with GUI')
    p.add_argument('-a', '--arch', default='x86_64',
                   help='i586 | x86_64 | aarch64 | loongarch64')
    p.add_argument('--bootmode', default='auto', choices=['iso', 'esp', 'floppy', 'kernel', 'auto'],
                   help='boot media (auto = per-arch default; kernel = -kernel/-append, x86 only)')
    p.add_argument('--accel', default=None, metavar='VALUE',
                   help="QEMU -accel value (default: %s)" % DEFAULT_ACCEL)
    p.add_argument('--image', metavar='PATH',
                   help='kernel bootmode image (default: build/<arch>/mt86plus)')
    p.add_argument('--no-reboot', action='store_true',
                   help='add -no-reboot (exit QEMU instead of rebooting the guest)')
    p.add_argument('--nic', action='append', choices=sorted(NICS), metavar='NAME',
                   help='add a user-mode NIC (repeatable): ' + ', '.join(sorted(NICS)))
    p.add_argument('--usb-msd', action='append', default=[], metavar='PATH',
                   help='attach a raw disk image or block device as a writable USB mass storage device (repeatable, max %d)' % MAX_USB_MSD)
    p.add_argument('--usb-host', action='append', default=[], metavar='SPEC',
                   help='attach a physical USB device via usb-host (repeatable): VID:PID, BUS.PORT, or QEMU key=value properties')
    p.add_argument('--pci-host', action='append', default=[], metavar='BDF',
                   help='attach a host PCI device via VFIO (repeatable, x86_64 only)')
    p.add_argument('--machine',
                   help='override -machine')
    p.add_argument('--cpu',
                   help='override -cpu')
    p.add_argument('--smp', default='1,1,1', metavar='M,N,P',
                   help='cores per socket, sockets, NUMA nodes (P must divide M*N); default 1,1,1')
    p.add_argument('--ram', type=int, metavar='MB',
                   help='guest RAM size (default: 1024, 2048 on x86_64)')
    p.add_argument('--cmdline',
                   help='kernel command line for the guest (x86 --bootmode floppy only)')
    p.add_argument('--output-dir',
                   help='work directory (default: temporary, removed on pass)')
    p.add_argument('--json', action='store_true',
                   help='print machine-readable summary (smoke/run)')
    p.add_argument('-t', '--time', type=int, default=None, metavar='SEC',
                   help='runtime cap in seconds (smoke: default 180; run mode: optional, unbounded otherwise)')
    p.add_argument('--dump-interval', type=float, default=1.0, metavar='SEC',
                   help='seconds between screendumps (default 1.0)')
    p.add_argument('--keep-dumps', action='store_true',
                   help='keep all dumps even on pass (default: only dumps/log/json on failure)')
    p.add_argument('--display', default=None, metavar='BACKEND',
                   help='QEMU display backend (default: gtk in run mode, none otherwise; overriding with raw-qemu "-display ..." also works, last one wins)')
    p.add_argument('--gdb', nargs='?', const=1234, default=None, type=int, metavar='PORT',
                   help='debug mode: gdb stub TCP port (default 1234)')
    p.add_argument('--gdb-cmd', metavar='BIN',
                   help='debug mode: gdb binary for the spawned terminal (default: gdb / gdb-multiarch)')
    p.add_argument('--terminal-cmd', metavar='CMD',
                   help='debug mode: terminal command to run gdb in, e.g. "xterm -e " or "gnome-terminal -- "')
    p.add_argument('--keys', metavar='KEYS',
                   help='comma-separated QEMU qcodes to send (e.g. "c,c", repeats as "down*3"); smoke: at 25%% of the runtime; run: after first paint, see --key-delay')
    p.add_argument('--key-delay', metavar='INITIAL,BETWEEN',
                   help='run mode: seconds before the first key after first paint and between successive keys (default: 1.5,0.5)')
    p.add_argument('--raw-qemu', metavar='ARGS',
                   help='extra raw arguments appended to the QEMU command line (escape hatch, e.g. "-numa dist ...")')
    p.add_argument('--clean', action='store_true',
                   help='debug mode: delete all debugging related files in the build directory and run "make clean"')
    p.add_argument('--dump-qemu-cmd', action='store_true',
                   help='print the QEMU command line and exit')
    return p.parse_args(argv)


def normalize_usb_msd(paths):
    if isinstance(paths, str):
        paths = [paths]
    paths = list(paths or [])
    if len(paths) > MAX_USB_MSD:
        die(EXIT_USAGE, "at most %d --usb-msd devices are supported" % MAX_USB_MSD)
    out = []
    for raw in paths:
        if not isinstance(raw, str):
            die(EXIT_USAGE, "--usb-msd expects a path")
        path = os.path.abspath(os.path.expanduser(raw))
        try:
            st = os.stat(path)
        except OSError as e:
            die(EXIT_USAGE, "--usb-msd %s: %s" % (path, e))
        if stat.S_ISREG(st.st_mode):
            if st.st_size == 0:
                die(EXIT_USAGE, "--usb-msd %s: empty image" % path)
        elif not stat.S_ISBLK(st.st_mode):
            die(EXIT_USAGE, "--usb-msd %s: not a regular file or block device" % path)
        if not os.access(path, os.R_OK):
            die(EXIT_USAGE, "--usb-msd %s: not readable" % path)
        if not os.access(path, os.W_OK):
            die(EXIT_USAGE, "--usb-msd %s: not writable" % path)
        out.append(path)
    return out


def _usb_host_property_entry(raw):
    props = {}
    selector = {}
    for item in raw.split(','):
        item = item.strip()
        if not item or '=' not in item:
            die(EXIT_USAGE, "--usb-host %s: malformed property %r" % (raw, item))
        key, value = item.split('=', 1)
        key = key.strip().lower()
        value = value.strip()
        if not key or not value:
            die(EXIT_USAGE, "--usb-host %s: empty property name or value" % raw)
        if key not in USB_HOST_KEYS:
            die(EXIT_USAGE, "--usb-host %s: unsupported property %r" % (raw, key))
        if key in props:
            die(EXIT_USAGE, "--usb-host %s: duplicate property %r" % (raw, key))
        if key in ('vendorid', 'productid'):
            try:
                number = int(value, 0)
            except ValueError:
                die(EXIT_USAGE, "--usb-host %s: invalid %s" % (raw, key))
            if not 0 <= number <= 0xffff:
                die(EXIT_USAGE, "--usb-host %s: %s out of range" % (raw, key))
            props[key] = '0x%04x' % number
            selector[key] = number
        elif key in ('hostbus', 'hostaddr'):
            try:
                number = int(value, 0)
            except ValueError:
                die(EXIT_USAGE, "--usb-host %s: invalid %s" % (raw, key))
            if number < 0:
                die(EXIT_USAGE, "--usb-host %s: %s must be non-negative" % (raw, key))
            props[key] = str(number)
            selector[key] = number
        elif key in ('hostport', 'hostdevice', 'serial'):
            props[key] = value
            selector[key] = value
        else:
            value = value.lower()
            if value not in ('on', 'off'):
                die(EXIT_USAGE, "--usb-host %s: %s must be on or off" % (raw, key))
            props[key] = value
    if ('hostport' in selector) != ('hostbus' in selector):
        die(EXIT_USAGE, "--usb-host %s: hostbus and hostport must be used together" % raw)
    if not (set(selector) & USB_HOST_SELECTORS):
        die(EXIT_USAGE, "--usb-host %s: no device selector" % raw)
    return {
        'text': raw,
        'qemu': ','.join('%s=%s' % (key, value) for key, value in props.items()),
        'selector': selector,
    }


def normalize_usb_host(specs):
    if isinstance(specs, str):
        specs = [specs]
    specs = list(specs or [])
    out = []
    for raw in specs:
        if not isinstance(raw, str):
            die(EXIT_USAGE, "--usb-host expects a selector string")
        raw = raw.strip()
        if not raw:
            die(EXIT_USAGE, "--usb-host expects a selector string")
        if '=' in raw:
            out.append(_usb_host_property_entry(raw))
            continue
        match = re.fullmatch(r'([0-9a-fA-F]{1,4}):([0-9a-fA-F]{1,4})', raw)
        if match:
            vendor, product = (int(value, 16) for value in match.groups())
            out.append({
                'text': raw,
                'qemu': 'vendorid=0x%04x,productid=0x%04x' % (vendor, product),
                'selector': {'vendorid': vendor, 'productid': product},
            })
            continue
        match = re.fullmatch(r'([0-9]+)\.([0-9]+(?:\.[0-9]+)*)', raw)
        if match:
            bus, port = match.groups()
            out.append({
                'text': raw,
                'qemu': 'hostbus=%d,hostport=%s' % (int(bus), port),
                'selector': {'hostbus': int(bus), 'hostport': port},
            })
            continue
        die(EXIT_USAGE, "--usb-host %s: expected VID:PID, BUS.PORT, or key=value properties" % raw)
    return out


def normalize_pci_host(specs):
    if isinstance(specs, str):
        specs = [specs]
    specs = list(specs or [])
    out = []
    seen = set()
    for raw in specs:
        if not isinstance(raw, str):
            die(EXIT_USAGE, "--pci-host expects a BDF string")
        raw = raw.strip().lower()
        match = PCI_BDF_RE.fullmatch(raw)
        if not match:
            die(EXIT_USAGE, "--pci-host %s: expected [domain:]bus:slot.function" % raw)
        domain, bus, slot, function = match.groups()
        bdf = '%04x:%s:%s.%s' % (int(domain or '0', 16), bus, slot, function)
        if bdf in seen:
            die(EXIT_USAGE, "--pci-host %s: duplicate device" % bdf)
        seen.add(bdf)
        out.append(bdf)
    return out


def usb_host_warnings(entries, sysfs='/sys'):
    warnings = []
    devices = os.path.join(sysfs, 'bus', 'usb', 'devices')
    if entries and not os.path.isdir(devices):
        return ["cannot inspect USB devices under %s" % devices]
    for entry in entries:
        selector = entry['selector']
        bus = selector.get('hostbus')
        port = selector.get('hostport')
        if bus is not None and port is not None:
            name = '%d-%s' % (bus, port)
            if not os.path.exists(os.path.join(devices, name)):
                warnings.append(
                    "--usb-host %s: no device at bus %d port %s (QEMU will leave it empty)"
                    % (entry['text'], bus, port))
            continue
        vendorid = selector.get('vendorid')
        productid = selector.get('productid')
        if vendorid is None and productid is None:
            continue
        matches = []
        for name in sorted(os.listdir(devices)):
            path = os.path.join(devices, name)
            try:
                with open(os.path.join(path, 'idVendor')) as f:
                    found_vendor = int(f.read().strip(), 16)
                with open(os.path.join(path, 'idProduct')) as f:
                    found_product = int(f.read().strip(), 16)
            except (OSError, ValueError):
                continue
            if vendorid is not None and vendorid != found_vendor:
                continue
            if productid is not None and productid != found_product:
                continue
            matches.append(name)
        if not matches:
            warnings.append(
                "--usb-host %s: no matching USB device (QEMU will leave it empty)"
                % entry['text'])
        elif len(matches) > 1:
            warnings.append(
                "--usb-host %s: %d matching USB devices; QEMU may attach the first one"
                % (entry['text'], len(matches)))
    return warnings


def _pci_driver(path):
    link = os.path.join(path, 'driver')
    if not os.path.exists(link):
        return None
    return os.path.basename(os.path.realpath(link))


def pci_host_problems(bdf, sysfs='/sys', dev_vfio='/dev/vfio'):
    path = os.path.join(sysfs, 'bus', 'pci', 'devices', bdf)
    if not os.path.exists(path):
        return ["--pci-host %s: host PCI device not found" % bdf]
    problems = []
    group_link = os.path.join(path, 'iommu_group')
    if not os.path.exists(group_link):
        return ["--pci-host %s: not in an IOMMU group; enable host IOMMU and reboot"
                % bdf]
    group = os.path.basename(os.path.realpath(group_link))
    driver = _pci_driver(path)
    if driver != 'vfio-pci':
        problems.append(
            "--pci-host %s: bound to %s, not vfio-pci"
            % (bdf, driver or 'no driver'))
    for name in ('vfio', group):
        node = os.path.join(dev_vfio, name)
        if not os.path.exists(node):
            problems.append(
                "--pci-host %s: missing %s (load vfio-pci or fix /dev/vfio permissions)"
                % (bdf, node))
        elif not os.access(node, os.R_OK | os.W_OK):
            problems.append(
                "--pci-host %s: %s is not readable/writable"
                % (bdf, node))
    group_devices = os.path.join(sysfs, 'kernel', 'iommu_groups', group, 'devices')
    if os.path.isdir(group_devices):
        for member in sorted(os.listdir(group_devices)):
            member_path = os.path.join(sysfs, 'bus', 'pci', 'devices', member)
            member_driver = _pci_driver(member_path)
            if member_driver and member_driver != 'vfio-pci':
                problems.append(
                    "--pci-host %s: IOMMU group %s member %s is bound to %s; "
                    "bind all group members to vfio-pci or leave them unbound"
                    % (bdf, group, member, member_driver))
    return problems


def pci_host_warnings(bdf, sysfs='/sys'):
    path = os.path.join(sysfs, 'bus', 'pci', 'devices', bdf, 'class')
    try:
        with open(path) as f:
            device_class = int(f.read().strip(), 0)
    except (OSError, ValueError):
        return []
    if device_class >> 16 != 0x02:
        return ["--pci-host %s: not a network controller; continuing anyway" % bdf]
    return []


def preflight_passthrough(cfg):
    usb_host = cfg.get('usb_host') or []
    if usb_host:
        if not os.path.isdir('/dev/bus/usb'):
            info("[warn] /dev/bus/usb is not available; QEMU may not access USB devices")
        for warning in usb_host_warnings(usb_host):
            info("[warn] " + warning)
    pci_host = cfg.get('pci_host') or []
    if not pci_host:
        return
    problems = []
    warnings = []
    for bdf in pci_host:
        problems += pci_host_problems(bdf)
        warnings += pci_host_warnings(bdf)
    for warning in warnings:
        info("[warn] " + warning)
    if problems:
        die(EXIT_USAGE, "PCI passthrough preflight failed:\n  " + "\n  ".join(problems))


def make_cfg(args):
    arch = args.arch
    if arch not in ARCHS:
        die(EXIT_USAGE, "unknown arch '%s' (known: %s)" % (args.arch, ', '.join(ARCHS)))
    spec = ARCHS[arch]
    bootmode = args.bootmode if args.bootmode != 'auto' else spec['bootmode']
    ram = args.ram if args.ram is not None else spec['ram']

    try:
        m, n, p_ = (int(x) for x in args.smp.split(','))
    except ValueError:
        die(EXIT_USAGE, "--smp expects three integers: M,N,P")
    if m < 1 or n < 1 or p_ < 1:
        die(EXIT_USAGE, "--smp values must be >= 1")
    cpus = m * n
    if cpus % p_ != 0:
        die(EXIT_USAGE, "--smp %s: P=%d must divide M*N=%d" % (args.smp, p_, cpus))

    nic_names = list(getattr(args, 'nic', None) or [])
    if isinstance(nic_names, str):
        nic_names = [nic_names]
    for name in nic_names:
        if name not in NICS:
            die(EXIT_USAGE, "unknown NIC '%s' (known: %s)" % (name, ', '.join(sorted(NICS))))
    usb_host = normalize_usb_host(getattr(args, 'usb_host', None))
    pci_host = normalize_pci_host(getattr(args, 'pci_host', None))
    if pci_host and arch != 'x86_64':
        die(EXIT_USAGE, "--pci-host is currently supported on x86_64 only")
    machine = args.machine or spec['machine']
    if not args.machine:
        for name in nic_names:
            if NICS[name]['machine'] == 'q35':
                machine = 'q35'
                break
        if pci_host:
            machine = 'q35'

    usb_msd = normalize_usb_msd(getattr(args, 'usb_msd', None))
    boot_image = getattr(args, 'image', None) or os.path.join('build', arch, 'mt86plus')
    cfg = {
        'arch': arch,
        'spec': spec,
        'bootmode': bootmode,
        'machine': machine,
        'cpu': args.cpu or spec['cpu'],
        'smp': (m, n, p_),
        'ram': ram,
        'build': os.path.abspath(os.path.join('build', arch)),
        'image': os.path.abspath(boot_image),
        'accel': getattr(args, 'accel', None) or DEFAULT_ACCEL,
        'no_reboot': bool(getattr(args, 'no_reboot', False)),
        'nics': nic_names,
        'usb_msd': usb_msd,
        'usb_host': usb_host,
        'pci_host': pci_host,
        'workdir': None,
        'display': args.display,
        'key_delay': (1.5, 0.5),
    }
    if args.key_delay:
        if args.mode != 'run':
            die(EXIT_USAGE, "--key-delay only applies to run mode")
        try:
            init, between = (float(x) for x in args.key_delay.split(','))
        except ValueError:
            die(EXIT_USAGE, "--key-delay expects two comma-separated floats: INITIAL,BETWEEN")
        if init < 0 or between < 0:
            die(EXIT_USAGE, "--key-delay values must be >= 0")
        cfg['key_delay'] = (init, between)
    if bootmode == 'floppy' and arch not in ('i586', 'x86_64'):
        die(EXIT_USAGE, "--bootmode floppy is only available on i586/x86_64")
    if bootmode == 'kernel' and arch not in ('i586', 'x86_64'):
        die(EXIT_USAGE, "--bootmode kernel is only available on i586/x86_64")
    if bootmode in ('esp', 'iso') and arch == 'i586':
        die(EXIT_USAGE, "i586 does not support UEFI boot here: no validated ia32 OVMF firmware. Use --bootmode floppy.")
    return cfg


def build_config(**kwargs):
    """Programmatic configuration for importers (e.g. tests/net/qemu_net_test.py).

    Accepts the same knobs as the command line without touching argparse.
    Unknown keys are rejected so callers fail loudly.
    """
    allowed = {
        'arch', 'bootmode', 'machine', 'cpu', 'smp', 'ram', 'cmdline',
        'display', 'key_delay', 'mode', 'nic', 'usb_msd', 'usb_host', 'pci_host',
        'accel', 'image', 'no_reboot', 'qemu',
    }
    unknown = set(kwargs) - allowed
    if unknown:
        die(EXIT_USAGE, "build_config: unknown key(s): %s" % ', '.join(sorted(unknown)))
    args = argparse.Namespace(
        arch=kwargs.get('arch', 'x86_64'),
        bootmode=kwargs.get('bootmode', 'auto'),
        machine=kwargs.get('machine'),
        cpu=kwargs.get('cpu'),
        smp=kwargs.get('smp', '1,1,1'),
        ram=kwargs.get('ram'),
        cmdline=kwargs.get('cmdline'),
        display=kwargs.get('display'),
        key_delay=kwargs.get('key_delay'),
        mode=kwargs.get('mode', 'smoke'),
        nic=kwargs.get('nic', []),
        usb_msd=kwargs.get('usb_msd', []),
        usb_host=kwargs.get('usb_host', []),
        pci_host=kwargs.get('pci_host', []),
        accel=kwargs.get('accel'),
        image=kwargs.get('image'),
        no_reboot=kwargs.get('no_reboot', False),
    )
    cfg = make_cfg(args)
    cfg['mode'] = args.mode
    cfg['cmdline'] = args.cmdline
    cfg['qemu'] = kwargs.get('qemu')
    return cfg


def smp_argv(cfg):
    """
    -smp M*N,cores=M,sockets=N ; if P > 1, split RAM into P NUMA nodes.

    (matches the style of upstream commit 507f7c4e5bc1, "hard-coded 8 NUMA
    nodes": per-node memory-backend-ram with prealloc=on, cpus ranges)
    """
    m, n, p_ = cfg['smp']
    cpus = m * n
    argv = ['-smp', '%d,cores=%d,sockets=%d' % (cpus, m, n)]
    if p_ > 1:
        if cfg['ram'] % p_ != 0:
            die(EXIT_USAGE, "--smp %s: P=%d must divide --ram %d" % (cfg['smp'], p_, cfg['ram']))
        per = cpus // p_
        size = cfg['ram'] // p_
        for k in range(p_):
            lo, hi = k * per, (k + 1) * per - 1
            argv += ['-object', 'memory-backend-ram,size=%dM,id=m%d,prealloc=on' % (size, k)]
            argv += ['-numa', 'node,nodeid=%d,memdev=m%d,cpus=%d-%d' % (k, k, lo, hi)]
    return argv


def stage_firmware(cfg, workdir, for_debug=False, dry=False):
    """
    Return (code, vars) firmware paths for UEFI bootmodes. A fresh writable
    copy of the vars file is staged; the code file is referenced in place
    as read-only.
    """
    spec = cfg['spec']
    u = spec['uefi']
    dirs = [d if d != '.' else cfg['build'] for d in u['dirs']]
    code = find_file(dirs, u['code'])
    if code is None:
        die(EXIT_USAGE, "no UEFI firmware found for %s (%s). %s" % (cfg['arch'], '/'.join(u['code']), u['hint']))
    vars_src = find_file(dirs, u['vars'])
    if vars_src is None:
        die(EXIT_USAGE, "no UEFI vars flash found for %s (%s). %s" % (cfg['arch'], '/'.join(u['vars']), u['hint']))
    vars_dst = os.path.join(workdir, 'efi_vars.fd')
    if dry:
        return code, vars_dst
    staging = cfg['build'] if for_debug else workdir
    vars_dst = os.path.join(staging, 'efi_vars.fd')
    shutil.copyfile(vars_src, vars_dst)
    return code, vars_dst


def legacy_bios_fw(cfg):
    spec = cfg['spec']
    for d, name in spec.get('legacy_bios_fw', []):
        if d == '.':
            p = name
        else:
            p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None


# ---------------------------------------------------------------------------
# build / staging

def ensure_built(cfg, need_iso=False, need_floppy=False):
    build = cfg['build']
    default_image = os.path.abspath(os.path.join(build, 'mt86plus'))
    if cfg['bootmode'] == 'kernel' and os.path.abspath(cfg['image']) != default_image:
        if not os.path.isfile(cfg['image']):
            die(EXIT_USAGE, "kernel image %s does not exist" % cfg['image'])
        return
    if not os.path.isdir(build):
        die(EXIT_USAGE, "no directory %s (run from the repo root)" % build)
    if not os.path.isfile(os.path.join(build, 'mt86plus')):
        info("[build] running make in %s ..." % build)
        rc = run_tool(['make', '-j%d' % os.cpu_count()], cwd=build)
        if rc != 0:
            die(EXIT_BUILDFAIL, "make failed in %s (rc=%d)" % (build, rc))
    missing = []
    if need_iso and not os.path.isfile(os.path.join(build, 'memtest.iso')):
        missing.append('iso')
    if need_floppy and not os.path.isfile(os.path.join(build, 'floppy.img')):
        missing.append('floppy.img')
    if missing:
        info("[build] running make %s in %s ..." % (' '.join(missing), build))
        rc = run_tool(['make', '-j%d' % os.cpu_count()] + missing, cwd=build)
        if rc != 0:
            die(EXIT_BUILDFAIL, "make %s failed in %s (rc=%d)" % (' '.join(missing), build, rc))


def stage_esp(cfg, outdir=None, img_path=None, dry=False):
    """
    Stage a FAT ESP image containing ::/EFI/BOOT/<efi_name> (mt86plus).
    Requires mkdosfs + mtools, which are checked by preflight().
    """
    spec = cfg['spec']
    if img_path is None:
        outdir = outdir or tempfile.gettempdir()
        img_path = os.path.join(outdir, 'esp.img')
    if dry:
        return img_path
    mkdosfs = find_tool(['mkdosfs', 'mkfs.fat'], ('/sbin', '/usr/sbin'))
    mmd = find_tool(['mmd'])
    mcopy = find_tool(['mcopy'])
    for t, p in (('mkdosfs', mkdosfs), ('mmd', mmd), ('mcopy', mcopy)):
        if p is None:
            die(EXIT_USAGE, "required tool '%s' not found; install the 'mtools' package" % t)
    rc = run_tool([mkdosfs, '-n', 'MEMTEST', '-F12', '-C', img_path, '4096'])
    if rc != 0:
        die(EXIT_USAGE, "mkdosfs failed (rc=%d)" % rc)
    for d in ('::/EFI', '::/EFI/BOOT'):
        rc = run_tool([mmd, '-i', img_path] + [d])
        if rc != 0:
            die(EXIT_USAGE, "mmd %s failed (rc=%d)" % (d, rc))
    src = os.path.join(cfg['build'], 'mt86plus')
    dst = '::/EFI/BOOT/' + spec['efi_name']
    rc = run_tool([mcopy, '-i', img_path, src, dst])
    if rc != 0:
        die(EXIT_USAGE, "mcopy %s failed (rc=%d)" % (dst, rc))
    return img_path


def boot_attach_argv(cfg, workdir, dry=False):
    """
    Media attach + boot firmware. Returns (argv, need_floppy).
    In debug mode (hda-contents staging) caller overrides the media part.
    """
    spec = cfg['spec']
    bootmode = cfg['bootmode']
    argv = []
    need_floppy = False
    if bootmode == 'floppy':
        need_floppy = True
        return argv, need_floppy  # floppy.img attached by caller via -fda
    if bootmode == 'kernel':
        return argv, need_floppy  # -kernel/-append handled by build_qemu_argv
    code, vars_fd = stage_firmware(cfg, workdir, dry=dry)
    argv += ['-drive', 'if=pflash,format=raw,readonly=on,file=%s' % code]
    argv += ['-drive', 'if=pflash,format=raw,file=%s' % vars_fd]
    if bootmode == 'esp':
        esp = stage_esp(cfg, outdir=workdir, dry=dry)
        if spec['attach'] == 'virtio':
            argv += ['-drive', 'if=virtio,file=%s,format=raw' % esp]
        elif spec['attach'] == 'nvme':
            argv += ['-drive', 'if=none,file=%s,format=raw,id=nv0' % esp, '-device', 'nvme,drive=nv0,serial=nvme0']
        else:
            die(EXIT_USAGE, "unknown attach mode %r" % spec['attach'])
    elif bootmode == 'iso':
        path = os.path.join(cfg['build'], 'memtest.iso')
        if cfg['arch'] in ('i586', 'x86_64'):
            argv += ['-cdrom', path]
        else:
            argv += ['-drive', 'if=none,format=raw,id=usb0,file=%s' % path, '-device', 'usb-storage,drive=usb0']
    return argv, need_floppy


def usb_controller_argv(cfg):
    """Add the default xHCI controller if a USB attachment needs one."""
    if not (cfg.get('usb_msd') or cfg.get('usb_host')):
        return []
    devices = cfg['spec']['devices']
    if any(d.split(',', 1)[0] in ('qemu-xhci', 'nec-usb-xhci') for d in devices):
        return []
    return ['-device', 'qemu-xhci']


def usb_msd_argv(cfg):
    """Attach prebuilt raw images through the default xHCI controller."""
    argv = []
    for index, path in enumerate(cfg.get('usb_msd') or []):
        drive = 'usbmsd%d' % index
        argv += ['-drive', 'if=none,id=%s,file=%s,format=raw' % (drive, path),
                 '-device', 'usb-storage,drive=%s,removable=on' % drive]
    return argv


def usb_host_argv(cfg):
    """Attach physical USB devices through the default xHCI controller."""
    argv = []
    for entry in cfg.get('usb_host') or []:
        argv += ['-device', 'usb-host,' + entry['qemu']]
    return argv


def pci_host_argv(cfg):
    """Attach host PCI devices via VFIO."""
    argv = []
    for bdf in cfg.get('pci_host') or []:
        argv += ['-device', 'vfio-pci,host=' + bdf]
    return argv


# --------------------------
# QMP + screendump machinery

class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(30)
        self.s.connect(path)
        self.buf = b''
        self.read()

    def _readline(self):
        while b'\n' not in self.buf:
            d = self.s.recv(65536)
            if not d:
                raise EOFError
            self.buf += d
        line, self.buf = self.buf.split(b'\n', 1)
        return line

    def read(self):
        return json.loads(self._readline())

    def cmd(self, c, **args):
        msg = {'execute': c}
        if args:
            msg['arguments'] = args
        self.s.sendall((json.dumps(msg) + '\n').encode())
        while True:
            r = self.read()
            if 'return' in r or 'error' in r:
                return r
            if r.get('event'):
                continue

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def connect_qmp(sock_path, proc):
    deadline = time.time() + QMP_WAIT_S
    while time.time() < deadline:
        if proc.poll() is not None:
            die(EXIT_BOOTFAIL, "QEMU exited before QMP came up (rc=%d)"
                % proc.returncode)
        if os.path.exists(sock_path):
            try:
                return QMP(sock_path)
            except (OSError, EOFError, ValueError):
                pass
        time.sleep(0.2)
    die(EXIT_BOOTFAIL, "no QMP socket after %ds" % QMP_WAIT_S)


def launch(cfg, workdir, extra_argv=(), gdb_port=None, paused=False,
           stderr_path=None):
    """Start QEMU for an importer and return (process, argv).

    extra_argv is appended after the common command line (netdevs, trace,
    filter-dump, -device intel-iommu, ...). The caller owns the process.
    """
    argv = build_qemu_argv(cfg, workdir, gdb_port=gdb_port, paused=paused)
    argv += list(extra_argv)
    if stderr_path:
        stderr = open(stderr_path, 'wb')
    else:
        stderr = subprocess.DEVNULL
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=stderr)
    return proc, argv


def stop(proc, qmp=None):
    """Tear down a QEMU process started by launch()."""
    if qmp is not None:
        try:
            qmp.cmd('quit')
        except Exception:
            pass
        qmp.close()
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    parts = data.split(None, 4)
    if len(parts) < 5 or parts[0] != b'P6':
        raise ValueError('not a P6 ppm: %s' % path)
    return int(parts[1]), int(parts[2]), parts[4]


def is_black(pix):
    return pix.count(0) == len(pix)


def unique_colors(pix, stride=8):
    n = len(pix) // 3
    colors = set()
    for i in range(0, n, stride):
        colors.add(pix[i * 3:i * 3 + 3])
    return len(colors)


def is_painted(fr):
    _, _, pix = fr
    return not is_black(pix) and unique_colors(pix) >= PAINT_MIN_COLORS


def diff_px(a, b):
    aw, ah, apix = a
    bw, bh, bpix = b
    if len(apix) != len(bpix) or (aw, ah) != (bw, bh):
        return len(apix) // 3   # whole-frame change (e.g. resolution switch)
    return sum(1 for i in range(0, len(apix), 3) if apix[i:i + 3] != bpix[i:i + 3])


def monitor(cfg, q, dumps_dir, duration, interval, keys, progress=True):
    """
    Dumps frames at `interval` and returns a result dict shared by
    smoke/run modes.  Runs the validated detector:
      - first meaningful paint (many colors, not all black)
      - after paint: sustained activity (>= K of last R dumps changed > 2 px)
      - frozen after paint (F dumps with zero change) => no progress
      - 2-sample sliding: garbage/black screendumps are tolerated because
        they simply fail to parse or produce huge diffs
    """
    xf = bool(cfg['spec'].get('xfail'))
    timeline = []
    frames = []
    first_paint = None
    recent = []
    inactive = 0
    active_after = 0
    frames_after = 0
    verdict = None
    rationale = ''

    t0 = time.time()
    t_end = None if duration is None else t0 + duration
    mode = cfg['mode']
    kd_init, kd_between = cfg.get('key_delay', (1.5, 0.5))
    if mode == 'run':
        next_key = None          # armed when the first paint is observed
    else:
        next_key = t0 + duration * KEYS_AT_FRAC
    ki = 0
    n = 0
    fails = 0

    while True:
        if t_end is not None and time.time() >= t_end:
            break
        if keys and next_key is not None and ki < len(keys) and time.time() >= next_key:
            k = keys[ki]
            ki += 1
            next_key = time.time() + (kd_between if mode == 'run' else interval)
            try:
                q.cmd('send-key', keys=[{'type': 'qcode', 'data': k}])
                if progress:
                    info('[%6.1fs] sent key %s' % (time.time() - t0, k))
            except Exception as e:
                info('[%6.1fs] send-key %r failed: %s' % (time.time() - t0, k, e))
        n += 1
        fn = os.path.join(dumps_dir, 'd%03d.ppm' % n)
        try:
            q.cmd('screendump', filename=fn)
        except Exception as e:
            info('[%6.1fs] screendump failed: %s' % (time.time() - t0, e))
            fails += 1
            if fails >= 3:
                return {'verdict': 'boot_failure',
                        'rationale': '3 consecutive screendump failures',
                        'first_paint_s': None, 'timeline': [],
                        'activity_ratio': 0.0, 'dumps': n}
            time.sleep(2)
            continue
        time.sleep(max(interval, 0.05))
        try:
            fr = read_ppm(fn)
        except Exception:
            continue          # tolerate occasional black/garbage dump
        delta = diff_px(frames[-1], fr) if frames else None
        timeline.append(delta)
        ts = time.time() - t0
        painted = is_painted(fr)
        if first_paint is None and painted:
            first_paint = ts
            if mode == 'run' and keys:
                next_key = time.time() + kd_init
            info('[%6.1fs] first meaningful paint (%dx%d)' % (ts, fr[0], fr[1]))
        frames.append(fr)
        del frames[:-3]
        if progress and delta is not None:
            info('[%6.1fs] d%03d %dx%d delta=%d painted=%s' % (ts, n, fr[0], fr[1], delta, painted))

        if mode != 'run' and first_paint is not None and delta is not None \
                and time.time() - t0 >= first_paint + SETTLE_S:
            frames_after += 1
            act = delta > ACTIVITY_MIN_PX
            if act:
                active_after += 1
                inactive = 0
            else:
                inactive += 1
            recent.append(act)
            del recent[:-WINDOW_R]
            if xf and inactive >= FREEZE_F:
                verdict = 'pass'
                rationale = ('static after paint (known port limitation, CPU is alive but screen updates are not emulated)')
                break
            if len(recent) >= WINDOW_R and sum(recent) >= WINDOW_K:
                verdict = 'pass'
                rationale = ('sustained activity: %d/%d recent dumps changed' % (sum(recent), len(recent)))
                break
            if inactive >= FREEZE_F:
                verdict = 'no_progress'
                rationale = ('frozen after paint: %d dumps without screen change' % inactive)
                break

    if verdict is None and first_paint is None:
        verdict = 'boot_failure'
        rationale = 'no meaningful paint within the runtime'
    elif verdict is None:
        # natural expiry of the runtime
        if mode == 'run':
            verdict = 'pass'
            rationale = ('interactive session ended; guest painted the UI at %.1fs' % first_paint)
        elif sum(recent[-WINDOW_R:]) >= WINDOW_K:
            verdict = 'pass'
            rationale = ('sustained activity: %d/%d recent dumps changed' % (sum(recent[-WINDOW_R:]), len(recent[-WINDOW_R:])))
        elif xf and first_paint is not None:
            verdict = 'pass'
            rationale = ('static after paint (known port limitation, CPU is alive but screen updates are not emulated)')
        else:
            verdict = 'no_progress'
            rationale = ('ended without sustained activity (%d/%d active after paint)' % (active_after, max(frames_after, 1)))

    return {'verdict': verdict, 'rationale': rationale,
            'first_paint_s': round(first_paint, 1) if first_paint else None,
            'timeline': timeline, 'activity_ratio': round(active_after / frames_after, 3) if frames_after else 0.0,
            'dumps': n}


# ---------------------------------------------------------------------------
# qemu command line

def build_qemu_argv(cfg, workdir, gdb_port=None, paused=False, dry=False):
    spec = cfg['spec']
    bootmode = cfg['bootmode']
    serial = os.path.join(workdir, 'serial.log')
    sock = os.path.join(workdir, 'qmp.sock')
    argv, need_floppy = boot_attach_argv(cfg, workdir, dry=dry)
    if bootmode == 'floppy' and need_floppy:
        argv += ['-fda', os.path.join(cfg['build'], 'floppy.img')]

    out = [cfg.get('qemu') or spec['qemu']]
    out += ['-accel', cfg.get('accel') or DEFAULT_ACCEL]
    out += ['-machine', cfg['machine']]
    if cfg['cpu']:
        out += ['-cpu', cfg['cpu']]
    out += ['-m', str(cfg['ram'])]
    out += smp_argv(cfg)
    for d in spec['devices']:
        out += ['-device', d]
    out += usb_controller_argv(cfg)
    out += usb_msd_argv(cfg)
    out += usb_host_argv(cfg)
    out += pci_host_argv(cfg)
    out += argv
    for index, name in enumerate(cfg.get('nics') or []):
        nic = NICS[name]
        out += ['-netdev', 'user,id=n%d' % index,
                '-device', '%s,netdev=n%d,mac=52:54:00:12:34:%02x'
                           % (nic['device'], index, 0x56 + index)]
    if cfg['mode'] == 'run':
        out += ['-display', cfg.get('display') or 'gtk', '-monitor', 'none']
    else:
        out += ['-display', cfg.get('display') or 'none', '-monitor', 'none']
    out += ['-serial', 'file:' + serial]
    out += ['-qmp', 'unix:%s,server=on,wait=off' % sock]
    if bootmode == 'kernel':
        out += ['-kernel', cfg['image']]
        if args_cmdline(cfg):
            out += ['-append', args_cmdline(cfg)]
    elif args_cmdline(cfg):
        if cfg['bootmode'] == 'floppy' and cfg['arch'] in ('i586', 'x86_64'):
            out += ['-kernel', os.path.join(cfg['build'], 'mt86plus'),
                    '-append', args_cmdline(cfg)]
        elif not cfg.get('warn_cmdline_dropped', False) and not cfg.get('qsilent'):
            cfg['warn_cmdline_dropped'] = True
            info("[warn] --cmdline is only supported for x86 BIOS (--bootmode floppy); dropping it")
    if cfg.get('no_reboot'):
        out += ['-no-reboot']
    if gdb_port is not None:
        out += ['-gdb', 'tcp::%d' % gdb_port]
    if paused:
        out += ['-S']
    if cfg['mode'] == 'debug' and not cfg.get('nics'):
        out += ['-net', 'none']
    raw = cfg.get('_raw', '')
    if raw:
        out += shlex.split(raw)
    return out


def args_cmdline(cfg):
    return cfg.get('cmdline')


def qemu_cmdline_text(cfg, workdir='<workdir>'):
    if cfg['mode'] == 'debug':
        gdb_port = cfg['gdb'] if cfg['gdb'] is not None else 1234
        argv = build_debug_argv(cfg, workdir, gdb_port, dry=True)
    else:
        argv = build_qemu_argv(cfg, workdir, dry=True)
    return ' '.join(shlex.quote(s) for s in argv)


# ---------------------------------------------------------------------------
# smoke / run

def expand_keys(spec):
    """'c,c*3,down' -> ['c', 'c', 'c', 'c', 'down']"""
    out = []
    for tok in spec:
        if '*' in tok:
            key, _, rep = tok.rpartition('*')
            if key and rep.isdigit() and int(rep) > 0:
                out.extend([key] * int(rep))
                continue
            die(EXIT_USAGE, "bad key repeat %r (expected KEY*COUNT, e.g. 'c*3')" % tok)
        out.append(tok)
    return out


def do_smoke_or_run(cfg):
    duration = float(cfg['time']) if cfg['time'] is not None else None
    interval = cfg['dump_interval']
    keys = expand_keys([k.strip() for k in cfg['keys'].split(',')]) if cfg['keys'] else []
    workdir = cfg['workdir']
    dumps = os.path.join(workdir, 'dumps')
    os.makedirs(dumps)
    sock = os.path.join(workdir, 'qmp.sock')

    argv = build_qemu_argv(cfg, workdir)
    info('[qemu] ' + ' '.join(shlex.quote(s) for s in argv))
    if os.path.exists(sock):
        os.unlink(sock)
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=open(os.path.join(workdir, 'qemu.err'), 'wb'))
    try:
        q = connect_qmp(sock, proc)
        q.cmd('qmp_capabilities')
        try:
            res = monitor(cfg, q, dumps, duration, interval, keys)
            try:
                q.cmd('quit')
            except Exception:
                pass
            q.close()
            try:
                proc.wait(timeout=10)
            except Exception:
                proc.kill()
                proc.wait()
        except Exception:
            q.close()
            proc.kill()
            proc.wait()
            raise
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    return res


def summarize(cfg, res):
    out = {
        'arch': cfg['arch'],
        'bootmode': cfg['bootmode'],
        'first_paint_s': res.get('first_paint_s'),
        'activity_ratio': res.get('activity_ratio'),
        'changed_px_timeline': res.get('timeline'),
        'qemu_exit': 0,
        'status': res['verdict'],
        'xfail': bool(cfg['spec'].get('xfail')),
        'rationale': res['rationale'],
        'artifacts': [],
    }
    return out


def cleanup_workdir(cfg, status):
    keep = cfg['keep_dumps'] or cfg['output_dir'] or status != 'pass'
    if not keep:
        shutil.rmtree(cfg['workdir'], ignore_errors=True)
        cfg['workdir'] = None


# ----------------------------------------------------------------
# debug mode (strict superset of the old build/*/debug_memtest.sh)

def image_base(cfg):
    header = cfg['spec']['header']
    if not os.path.isfile(header):
        die(EXIT_USAGE, "cannot read IMAGE_BASE: %s missing" % header)
    with open(header) as f:
        txt = f.read()
    m = re.search(r'#define\s+IMAGE_BASE\s+(0x[0-9A-Fa-f]+)', txt)
    if not m:
        die(EXIT_USAGE, "cannot read IMAGE_BASE from %s" % header)
    return int(m.group(1), 16)


def sym_addr(binary, name):
    r = subprocess.run(['objdump', '-t', binary], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for line in r.stdout.decode(errors='replace').splitlines():
        f = line.split()
        if len(f) >= 5 and f[-1] == name:
            return int(f[0], 16)
    die(EXIT_USAGE, "cannot find symbol %s in %s" % (name, binary))


def prep_lds(cfg):
    lds = os.path.join(cfg['build'], 'ldscripts', 'memtest_shared.lds')
    if not os.path.isfile(lds):
        die(EXIT_USAGE, "'%s' does not exist" % lds)
    with open(lds) as f:
        txt = f.read()
    out = os.path.join(cfg['build'], 'memtest_shared_debug.lds')
    with open(out, 'w') as f:
        f.write('\n'.join(l for l in txt.splitlines() if 'DISCARD' not in l) + '\n')


def make_debug(cfg):
    rc = run_tool(['make', 'debug', 'DEBUG=1'], cwd=cfg['build'])
    if rc != 0:
        die(EXIT_BUILDFAIL, "make debug DEBUG=1 failed in %s (rc=%d)" % (cfg['build'], rc))


def emit_gdbscript(cfg, port):
    """
    Identical semantics to the old scripts: the file is only created when it
    does not exist, so a hand-written 'gdbscript' in the build directory is
    honored unchanged (gdb runs with cwd = build dir, so 'memtest.debug' is
    a relative path).
    """
    gdb_script = os.path.join(cfg['build'], 'gdbscript')
    if os.path.isfile(gdb_script):
        info("[gdb] existing gdbscript honored: %s" % gdb_script)
        return gdb_script
    ib = image_base(cfg)
    offset = ib + BASE_OF_CODE
    offset_str = '0x%X' % offset
    reloc_str = '0x%X' % RELOCADDR
    lines = ['set pagination off']
    if cfg['spec']['dual_text']:
        mtdebug = os.path.join(cfg['build'], 'memtest.debug')
        data = sym_addr(mtdebug, '_data')
        bss = sym_addr(mtdebug, '_bss')
        dataoff_str = '0x%X' % (offset + data)
        datarel_str = '0x%X' % (RELOCADDR + data)
        bssoff_str = '0x%X' % (offset + bss)
        lines += [
            'add-symbol-file memtest.debug %s -s .data %s -s .bss %s' % (offset_str, dataoff_str, bssoff_str),
            'add-symbol-file memtest.debug %s -s .data %s -s .bss %s' % (reloc_str, datarel_str, bssoff_str),
        ]
    elif cfg['arch'] == 'i586':
        mtdebug = os.path.join(cfg['build'], 'memtest.debug')
        data = sym_addr(mtdebug, '_data')
        dataoff_str = '0x%X' % (offset + data)
        datarel_str = '0x%X' % (RELOCADDR + data)
        lines += [
            'add-symbol-file memtest.debug %s -s .data %s' % (offset_str, dataoff_str),
            'add-symbol-file memtest.debug %s -s .data %s' % (reloc_str, datarel_str),
        ]
    else:
        # single load image (aarch64/loongarch64) - assumed loaded at its
        # preferred base by the UEFI firmware (debug for these ports is
        # new functionality provided by this script)
        lines += ['add-symbol-file memtest.debug %s' % offset_str]
    lines += [
        'b main',
        'commands',
        'layout src',
        'delete 1',
        'end',
        'b run_at',
        'shell sleep 0.5',
        'target remote localhost:%d' % port,
        'info b',
        'c',
    ]
    info("[gdb] creating gdbscript (offset %s)..." % offset_str)
    with open(gdb_script, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return gdb_script


def stage_hda(cfg, dry=False):
    """UEFI debug staging, mirroring the old Prepare_Directory()."""
    spec = cfg['spec']
    src = os.path.join(cfg['build'], 'mt86plus')
    hda = os.path.join(cfg['build'], 'hda-contents')
    if dry:
        return hda
    if not os.path.isfile(src):
        die(EXIT_BUILDFAIL, "%s missing" % src)
    os.makedirs(os.path.join(hda, 'EFI', 'boot'), exist_ok=True)
    shutil.copyfile(src, os.path.join(hda, 'mt86plus.efi'))
    shutil.copyfile(src, os.path.join(hda, 'EFI', 'boot', spec['efi_name']))
    return hda


def stage_uefi_debug_firmware(cfg, dry=False):
    """
    Locate the firmware for UEFI debug sessions, stage a writable vars copy
    in the build directory (like the old OVMF copies) and return the
    corresponding qemu firmware argv.  On x86 a legacy combined OVMF.fd is
    additionally passed via -bios, matching the old script.
    """
    spec = cfg['spec']
    u = spec['uefi']
    dirs = [d if d != '.' else cfg['build'] for d in u['dirs']]
    code = find_file(dirs, u['code'])
    varsfd = find_file(dirs, u['vars'])
    if code is None:
        die(EXIT_USAGE, "no UEFI firmware found for %s (%s). %s" % (cfg['arch'], '/'.join(u['code']), u['hint']))
    if varsfd is None:
        die(EXIT_USAGE, "no UEFI vars flash found for %s (%s). %s" % (cfg['arch'], '/'.join(u['vars']), u['hint']))
    vars_dst = os.path.join(cfg['build'], 'efi_vars_debug.fd')
    if not dry:
        shutil.copyfile(varsfd, vars_dst)
    argv = ['-drive', 'if=pflash,format=raw,readonly=on,file=%s' % code,
            '-drive', 'if=pflash,format=raw,file=%s' % vars_dst]
    legacy = legacy_bios_fw(cfg)
    if legacy:
        argv += ['-bios', legacy]
    return argv


def terminal_command(cfg):
    if cfg['terminal_cmd']:
        return shlex.split(cfg['terminal_cmd'])
    for binary, prefix in TERMINALS:
        if shutil.which(binary):
            info("[gdb] terminal found: %s" % binary)
            return shlex.split(prefix)
    return None


def clear_debug_files(cfg):
    build = cfg['build']
    debug_junk = ['debug.log', 'debug_serial.log', 'gdbscript', 'qmp.sock',
                  'QemuKill', 'OVMF.fd',
                  'OVMF_CODE.fd', 'OVMF_VARS.fd', 'OVMF32_CODE.fd',
                  'OVMF32_VARS.fd', 'memtest_shared_debug.lds',
                  'esp_debug.img', 'efi_vars_debug.fd', 'aavmf_code_debug.fd',
                  'aavmf_vars_debug.fd', 'la64_code_debug.fd',
                  'la64_vars_debug.fd']
    info("Deleting debug files...")
    shutil.rmtree(os.path.join(build, 'hda-contents'), ignore_errors=True)
    for n in debug_junk:
        p = os.path.join(build, n)
        if os.path.isfile(p) or os.path.exists(p) and not os.path.isdir(p):
            os.unlink(p)
    rc = run_tool(['make', 'clean'], cwd=build)
    if rc != 0:
        info("make clean failed (rc=%d)" % rc)


def build_debug_argv(cfg, workdir, gdb_port, dry=False):
    """QEMU argv for debug mode (gdb stub, paused, QMP + serial active)."""
    spec = cfg['spec']
    build = cfg['build']
    bootmode = cfg['bootmode']
    if bootmode in ('esp', 'iso'):
        if cfg['arch'] in ('i586', 'x86_64'):
            # hda-contents staging with parallel pflash/bios firmware,
            # mirroring the old Prepare_Directory()
            stage_hda(cfg, dry=dry)
            fw = stage_uefi_debug_firmware(cfg, dry=dry)
            media = ['-hda', 'fat:rw:hda-contents']
        else:
            # aarch64/la64: use the validated drive attachments
            fw = stage_uefi_debug_firmware(cfg, dry=dry)
            esp = stage_esp(cfg, img_path=os.path.join(build, 'esp_debug.img'), dry=dry)
            if spec['attach'] == 'virtio':
                media = ['-drive', 'if=virtio,file=%s,format=raw' % esp]
            else:
                media = ['-drive', 'if=none,file=%s,format=raw,id=nv0' % esp,
                         '-device', 'nvme,drive=nv0,serial=nvme0']
    elif bootmode == 'floppy':
        fw = []
        media = ['-fda', os.path.join(build, 'floppy.img')]
        info("[qemu] NOTE: BIOS/floppy debugging relies on the same offset table; symbols match only after the image reaches its load location.")
    else:
        die(EXIT_USAGE, "debug mode needs --bootmode esp (or floppy on x86)")

    argv = [spec['qemu']]
    argv += TCG
    argv += ['-machine', cfg['machine']]
    if cfg['cpu']:
        argv += ['-cpu', cfg['cpu']]
    argv += ['-m', str(cfg['ram'])]
    argv += smp_argv(cfg)
    for d in spec['devices']:
        argv += ['-device', d]
    argv += usb_controller_argv(cfg)
    argv += usb_msd_argv(cfg)
    argv += usb_host_argv(cfg)
    argv += pci_host_argv(cfg)
    argv += media
    argv += fw
    argv += ['-net', 'none', '-display', 'none', '-monitor', 'none']
    argv += ['-serial', 'file:' + os.path.join(build, 'debug_serial.log')]
    argv += ['-qmp', 'unix:%s,server=on,wait=off' % os.path.join(workdir, 'qmp.sock')]
    argv += ['-gdb', 'tcp::%d' % gdb_port, '-S']
    raw = cfg.get('_raw', '')
    if raw:
        argv += shlex.split(raw)
    return argv


def do_debug(cfg):
    """Set up and launch a gdb debugging session (all four arches)."""
    spec = cfg['spec']
    build = cfg['build']
    if cfg['clean']:
        clear_debug_files(cfg)
        return

    gdb_port = cfg['gdb'] if cfg['gdb'] is not None else 1234
    gdb_bin = cfg['gdb_cmd'] or find_tool(spec['gdbcands'])
    if gdb_bin is None:
        die(EXIT_USAGE, "no gdb found (tried %s); install one or pass --gdb-cmd" % ', '.join(spec['gdbcands']))

    # linker script, build, gdbscript, staging  (same order as old scripts)
    prep_lds(cfg)
    make_debug(cfg)
    bootmode = cfg['bootmode']
    if bootmode == 'floppy':
        ensure_built(cfg, need_floppy=True)
    emit_gdbscript(cfg, gdb_port)

    argv = build_debug_argv(cfg, build, gdb_port)
    info('[qemu] ' + ' '.join(shlex.quote(s) for s in argv))
    sock = os.path.join(build, 'qmp.sock')
    if os.path.exists(sock):
        os.unlink(sock)
    proc = subprocess.Popen(argv, cwd=build)
    term = terminal_command(cfg)
    if term is not None:
        info("[debug] waiting for gdb to connect on port %d (QEMU is paused until then); pid=%d" % (gdb_port, proc.pid))
        gdb_argv = term + [gdb_bin, '-x', 'gdbscript']
        subprocess.Popen(gdb_argv, cwd=build)
        try:
            proc.wait()
        except KeyboardInterrupt:
            proc.kill()
    else:
        info("[debug] no terminal emulator found; running gdb in the foreground (fallback; pass --terminal-cmd to override)")
        try:
            subprocess.run([gdb_bin, '-x', 'gdbscript'], cwd=build)
        except KeyboardInterrupt:
            pass
        if proc.poll() is None:
            subprocess.run(['kill', '-9', str(proc.pid)])
            proc.wait(timeout=10)


# ---------------------------------------------------------------------------
# main

def main(argv=sys.argv[1:]):
    args = parse_args(argv)
    try:
        cfg = make_cfg(args)
        cfg['mode'] = args.mode
        cfg['keep_dumps'] = args.keep_dumps
        cfg['output_dir'] = args.output_dir
        cfg['time'] = args.time if args.time is not None else (None if args.mode == 'run' else 180)
        cfg['dump_interval'] = args.dump_interval
        cfg['keys'] = args.keys
        cfg['terminal_cmd'] = args.terminal_cmd
        cfg['gdb'] = args.gdb
        cfg['gdb_cmd'] = args.gdb_cmd
        cfg['clean'] = args.clean
        cfg['cmdline'] = args.cmdline
        cfg['_raw'] = args.raw_qemu or ''
        cfg['json'] = args.json

        # --dump-qemu-cmd: print and exit, no preflight/build needed
        if args.dump_qemu_cmd:
            cfg['qsilent'] = True
            wd = args.output_dir or os.path.join(tempfile.gettempdir(), 'mtqemu-' + cfg['arch'])
            print(qemu_cmdline_text(cfg, wd))
            return 0

        if args.mode == 'debug':
            preflight_passthrough(cfg)
            do_debug(cfg)
            return 0

        # smoke / run
        preflight(cfg)
        need_floppy = cfg['bootmode'] == 'floppy'
        need_iso = cfg['bootmode'] == 'iso'
        ensure_built(cfg, need_iso=need_iso, need_floppy=need_floppy)

        if args.output_dir:
            wd = args.output_dir
            os.makedirs(wd, exist_ok=True)
        else:
            wd = tempfile.mkdtemp(prefix='mtqemu-%s-' % cfg['arch'])
        cfg['workdir'] = wd
        res = do_smoke_or_run(cfg)
        summary = summarize(cfg, res)
        keep = cfg['keep_dumps'] or cfg['output_dir'] or summary['status'] != 'pass'
        if keep:
            rp = os.path.join(wd, 'result.json')
            with open(rp, 'w') as f:
                json.dump(summary, f, indent=2)
            summary['artifacts'] = [os.path.join(wd, 'serial.log'), rp, os.path.join(wd, 'dumps')]
            summary['artifacts'] = [p for p in summary['artifacts'] if os.path.exists(p)]
        else:
            shutil.rmtree(wd, ignore_errors=True)
        if cfg['json']:
            print(json.dumps(summary, indent=2))
        else:
            print('RESULT: %s (%s) arch=%s boot=%s xfail=%s' % (summary['status'], summary['rationale'], summary['arch'], summary['bootmode'], summary['xfail']))
            if summary['artifacts']:
                print('artifacts kept in %s' % wd)
        codes = {'pass': EXIT_OK, 'boot_failure': EXIT_BOOTFAIL, 'no_progress': EXIT_NOPROGRESS}
        return codes.get(summary['status'], EXIT_NOPROGRESS)
    except Fail as e:
        print('mtqemu: error: %s' % e.msg, file=sys.stderr)
        return e.code
    except KeyboardInterrupt:
        return 130


def preflight(cfg):
    """Check all external tools/firmware before doing heavy work."""
    preflight_passthrough(cfg)
    spec = cfg['spec']
    bootmode = cfg['bootmode']
    qemu = cfg.get('qemu') or spec['qemu']
    missing = []
    if find_tool([qemu]) is None:
        missing.append((qemu, "install qemu-system-%s" % ('i386' if qemu.endswith('i386') else qemu.split('-')[-1])))
    if bootmode == 'esp':
        if find_tool(['mkdosfs', 'mkfs.fat'], ('/sbin', '/usr/sbin')) is None:
            missing.append(('mkdosfs', "install the 'dosfstools' package"))
        if find_tool(['mmd']) is None or find_tool(['mcopy']) is None:
            missing.append(('mcopy/mmd', "install the 'mtools' package"))
    if bootmode == 'iso' and find_tool(['xorrisofs', 'xorriso']) is None:
        missing.append(('xorrisofs', "install the 'xorriso' package (needed by the 'make iso' target)"))
    if missing:
        lines = ['precondition check failed:']
        for tool, hint in missing:
            lines.append("  missing tool: %s  (%s)" % (tool, hint))
        die(EXIT_USAGE, '\n'.join(lines))


if __name__ == '__main__':
    sys.exit(main())
