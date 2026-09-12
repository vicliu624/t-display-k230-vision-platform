#!/usr/bin/env python3
"""Fast host-side contract tests; the shell companion tests the real parser."""
from pathlib import Path
import configparser
import os
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "user-space/tdvp-labwc-desktop/src/menus"


class ApplicationMenu(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix" and shutil.which("make"), "Linux make required")
    def test_production_recipe_installs_all_menu_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            build = work / "build"
            shutil.copytree(SOURCE.parent, build)
            for helper in ("tdvp-key-bridge", "tdvp-gdk-committed-compat.so"):
                (build / helper).write_text("install-only fixture\n")
            recipe = ROOT / "buildroot/k230-sdk-overlay/package/tdvp-labwc-desktop/tdvp-labwc-desktop.mk"
            target = work / "target"
            makefile = work / "Makefile"
            makefile.write_text(f"include {recipe}\n.PHONY: {build}/install\n{build}/install:\n"
                                "\t$(TDVP_LABWC_DESKTOP_INSTALL_TARGET_CMDS)\n")
            subprocess.run(["make", "--no-print-directory", "-f", str(makefile),
                            "INSTALL=install", f"TARGET_DIR={target}", f"{build}/install"],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual((target / "etc/xdg/menus/lxde-applications.menu").read_bytes(),
                             (SOURCE / "lxde-applications.menu").read_bytes())
            for menu in self.menus.values():
                name = menu.findtext("Directory")
                self.assertEqual((target / "usr/share/desktop-directories" / name).read_bytes(),
                                 (SOURCE / name).read_bytes())

    def setUp(self):
        self.root = ET.parse(SOURCE / "lxde-applications.menu").getroot()
        self.menus = {m.findtext("Name"): m for m in self.root.findall("Menu")}

    def test_layout_reaches_every_category(self):
        layout = [m.text for m in self.root.findall("Layout/Menuname")]
        self.assertEqual(set(layout), set(self.menus))
        self.assertEqual(len(layout), len(set(layout)))
        self.assertEqual(layout[-1], "Other")

    def test_standard_categories(self):
        for category, menu in {
            "Utility": "Accessories", "Network": "Internet", "AudioVideo": "Sound & Video",
            "Audio": "Sound & Video", "Video": "Sound & Video", "Graphics": "Graphics",
            "Office": "Office", "Development": "Development", "Education": "Education",
            "Science": "Science", "Game": "Games", "Settings": "Preferences", "System": "System",
        }.items():
            self.assertIn(category, [c.text for c in self.menus[menu].findall("Include/Category")])

    def test_future_apps_have_unallocated_fallback(self):
        self.assertIsNotNone(self.root.find("DefaultAppDirs"))
        self.assertIsNotNone(self.menus["Other"].find("OnlyUnallocated"))
        self.assertIsNotNone(self.menus["Other"].find("Include/All"))
        self.assertEqual([m.findtext("Name") for m in self.root.findall("Menu")
                          if m.find("Include/All") is not None], ["Other"])

    def test_directory_assets_and_existing_entries(self):
        for menu in self.menus.values():
            asset = SOURCE / menu.findtext("Directory")
            config = configparser.ConfigParser(interpolation=None)
            config.read(asset, encoding="utf-8")
            self.assertEqual(config["Desktop Entry"]["Type"], "Directory")
            self.assertTrue(config["Desktop Entry"]["Name"])
            self.assertTrue(config["Desktop Entry"]["Icon"])
        entries = [e.text for e in self.root.findall(".//Include/Filename")]
        self.assertIn("foot.desktop", entries)
        self.assertIn("vpl-package-manager.desktop", entries)
        self.assertNotIn("vpl-camera.desktop", entries)
        self.assertNotIn("tdvp-netsurf.desktop", entries)


if __name__ == "__main__":
    unittest.main()
