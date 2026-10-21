#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import mtqemu


def make_pci(root, bdf='0000:07:00.0', group='42', driver='vfio-pci'):
    dev = root / 'bus' / 'pci' / 'devices' / bdf
    dev.mkdir(parents=True)
    (dev / 'class').write_text('0x020000\n')
    group_dir = root / 'kernel' / 'iommu_groups' / group
    (group_dir / 'devices').mkdir(parents=True)
    os.symlink(dev, group_dir / 'devices' / bdf)
    os.symlink(group_dir, dev / 'iommu_group')
    drv = root / 'bus' / 'pci' / 'drivers' / driver
    drv.mkdir(parents=True, exist_ok=True)
    os.symlink(drv, dev / 'driver')
    vfio = root / 'dev' / 'vfio'
    vfio.mkdir(parents=True)
    (vfio / 'vfio').touch()
    (vfio / group).touch()
    return dev, group_dir, vfio


def add_group_member(root, group_dir, bdf, driver):
    dev = root / 'bus' / 'pci' / 'devices' / bdf
    dev.mkdir(parents=True)
    os.symlink(dev, group_dir / 'devices' / bdf)
    drv = root / 'bus' / 'pci' / 'drivers' / driver
    drv.mkdir(parents=True, exist_ok=True)
    os.symlink(drv, dev / 'driver')


def add_usb(root, name, vendor, product):
    dev = root / 'bus' / 'usb' / 'devices' / name
    dev.mkdir(parents=True)
    (dev / 'idVendor').write_text('%04x\n' % vendor)
    (dev / 'idProduct').write_text('%04x\n' % product)


class UsbHostTests(unittest.TestCase):
    def test_vid_pid(self):
        entry, = mtqemu.normalize_usb_host(['1A86:7523'])
        self.assertEqual(entry['qemu'], 'vendorid=0x1a86,productid=0x7523')
        self.assertEqual(entry['selector'], {'vendorid': 0x1a86, 'productid': 0x7523})

    def test_bus_port(self):
        entry, = mtqemu.normalize_usb_host(['3.2.1'])
        self.assertEqual(entry['qemu'], 'hostbus=3,hostport=2.1')
        self.assertEqual(entry['selector'], {'hostbus': 3, 'hostport': '2.1'})

    def test_property_list(self):
        entry, = mtqemu.normalize_usb_host(
            ['vendorid=0x1a86,productid=0x7523,guest-reset=off'])
        self.assertEqual(
            entry['qemu'], 'vendorid=0x1a86,productid=0x7523,guest-reset=off')
        self.assertEqual(entry['selector'], {'vendorid': 0x1a86, 'productid': 0x7523})

    def test_rejects_bad_properties(self):
        for spec in (
            'guest-reset=off',
            'vendorid=0x1a86,id=usb0',
            'hostport=2.1',
            'hostbus=3',
            'vendorid=0x1a86,vendorid=0x1a86',
            '',
        ):
            with self.subTest(spec=spec):
                with self.assertRaises(mtqemu.Fail):
                    mtqemu.normalize_usb_host([spec])

    def test_warnings(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            add_usb(root, '3-2.1', 0x1a86, 0x7523)
            add_usb(root, '3-2.2', 0x1a86, 0x7523)
            port, = mtqemu.normalize_usb_host(['3.2.1'])
            missing, = mtqemu.normalize_usb_host(['3.2.3'])
            vidpid, = mtqemu.normalize_usb_host(['1a86:7523'])
            missing_id, = mtqemu.normalize_usb_host(['1a86:9999'])
            self.assertEqual(mtqemu.usb_host_warnings([port], str(root)), [])
            self.assertTrue(mtqemu.usb_host_warnings([missing], str(root)))
            self.assertTrue(mtqemu.usb_host_warnings([vidpid], str(root)))
            self.assertTrue(mtqemu.usb_host_warnings([missing_id], str(root)))

    def test_argv(self):
        entry, = mtqemu.normalize_usb_host(['1a86:7523'])
        cfg = {'spec': {'devices': []}, 'usb_msd': [], 'usb_host': [entry]}
        self.assertEqual(mtqemu.usb_controller_argv(cfg), ['-device', 'qemu-xhci'])
        self.assertEqual(
            mtqemu.usb_host_argv(cfg),
            ['-device', 'usb-host,vendorid=0x1a86,productid=0x7523'])
        cfg['spec']['devices'] = ['qemu-xhci']
        self.assertEqual(mtqemu.usb_controller_argv(cfg), [])


class PciHostTests(unittest.TestCase):
    def test_bdf_normalization(self):
        self.assertEqual(
            mtqemu.normalize_pci_host(['07:00.0', '0000:07:00.1']),
            ['0000:07:00.0', '0000:07:00.1'])

    def test_rejects_bad_bdf(self):
        for spec in ('7:0.0', '0000:07:00.8', '0000:07:00', '0000:07:00.0junk'):
            with self.subTest(spec=spec):
                with self.assertRaises(mtqemu.Fail):
                    mtqemu.normalize_pci_host([spec])

    def test_rejects_duplicate(self):
        with self.assertRaises(mtqemu.Fail):
            mtqemu.normalize_pci_host(['07:00.0', '0000:07:00.0'])

    def test_arch_guard(self):
        with self.assertRaises(mtqemu.Fail):
            mtqemu.build_config(arch='aarch64', pci_host=['0000:07:00.0'])

    def test_build_config(self):
        cfg = mtqemu.build_config(
            arch='x86_64', usb_host=['1a86:7523'], pci_host=['07:00.0'])
        self.assertEqual(cfg['machine'], 'q35')
        self.assertEqual(cfg['pci_host'], ['0000:07:00.0'])
        self.assertEqual(cfg['usb_host'][0]['qemu'],
                         'vendorid=0x1a86,productid=0x7523')

    def test_problems_and_warnings(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dev, group_dir, vfio = make_pci(root)
            self.assertEqual(
                mtqemu.pci_host_problems('0000:07:00.0', str(root), str(vfio)), [])
            self.assertEqual(mtqemu.pci_host_warnings('0000:07:00.0', str(root)), [])

            (dev / 'class').write_text('0x030000\n')
            self.assertTrue(mtqemu.pci_host_warnings('0000:07:00.0', str(root)))

            add_group_member(root, group_dir, '0000:07:00.1', 'e1000e')
            problems = mtqemu.pci_host_problems(
                '0000:07:00.0', str(root), str(vfio))
            self.assertTrue(any('member 0000:07:00.1' in p for p in problems))

    def test_missing_iommu_and_wrong_driver(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dev, _, vfio = make_pci(root)
            (dev / 'iommu_group').unlink()
            problems = mtqemu.pci_host_problems(
                '0000:07:00.0', str(root), str(vfio))
            self.assertTrue(any('IOMMU group' in p for p in problems))

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dev, _, vfio = make_pci(root)
            (dev / 'driver').unlink()
            other = root / 'bus' / 'pci' / 'drivers' / 'e1000e'
            other.mkdir(parents=True)
            os.symlink(other, dev / 'driver')
            problems = mtqemu.pci_host_problems(
                '0000:07:00.0', str(root), str(vfio))
            self.assertTrue(any('not vfio-pci' in p for p in problems))

    def test_argv(self):
        cfg = {'pci_host': ['0000:07:00.0', '0000:07:00.1']}
        self.assertEqual(
            mtqemu.pci_host_argv(cfg),
            ['-device', 'vfio-pci,host=0000:07:00.0',
             '-device', 'vfio-pci,host=0000:07:00.1'])


if __name__ == '__main__':
    unittest.main()
