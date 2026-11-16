#!/usr/bin/env python3
"""Ventuno: build and flash the DeskBuddy firmware.

The companion's firmware lives outside this repo:

    /home/arduino/addtional/DeskBuddy_v3_21_cloud/DeskBuddy

This helper builds it with PlatformIO and puts it on the device, so
flashing v3.21 (the release that adds the USB command channel, the
three-part face and the touch reporting) is one command:

    flash_buddy.py build          # compile only
    flash_buddy.py flash          # compile + flash over the USB cable
    flash_buddy.py ota            # compile + flash over Wi-Fi
    flash_buddy.py status         # what the device says right now

The USB flash needs the device in bootloader mode: hold BOOT, tap RESET,
release BOOT.  The OTA flash needs the buddy's Wi-Fi (its own password is
OTA_PASSWORD in include/wifi_credentials.h).

The build refuses to run with the placeholder Wi-Fi credentials (the
firmware's own static_assert does that too) - fill them in first.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

def _default_project():
    """The firmware tree.

    Inside the board repo it is the original checkout; inside the release
    folder (dist/deskbuddy/tools/flash_buddy.py) it is the folder above
    `tools/`, so the release copy needs no environment variable."""
    env = os.environ.get("VENTUNO_BUDDY_PROJECT")
    if env:
        return pathlib.Path(env)
    original = pathlib.Path(
        "/home/arduino/addtional/DeskBuddy_v3_21_cloud/DeskBuddy")
    if (original / "platformio.ini").exists():
        return original
    return pathlib.Path(__file__).resolve().parents[1]


PROJECT = _default_project()
PIO = os.environ.get("VENTUNO_PIO",
                     "/home/arduino/ventuno/venv/bin/pio")
CORE = os.environ.get("PLATFORMIO_CORE_DIR",
                      str(pathlib.Path.home() / ".platformio"))
SERIAL = os.environ.get("VENTUNO_BUDDY_SERIAL", "/dev/ventuno-face")
OTA_HOST = os.environ.get("VENTUNO_BUDDY_OTA", "192.168.29.35")
OTA_PORT = int(os.environ.get("VENTUNO_BUDDY_OTA_PORT", "8080"))


def _run(args, timeout=900, cwd=None, env=None):
    e = dict(os.environ)
    e["PLATFORMIO_CORE_DIR"] = CORE
    if env:
        e.update(env)
    try:
        return subprocess.run(args, capture_output=True, text=True,
                              timeout=timeout, cwd=cwd, env=e)
    except (OSError, subprocess.SubprocessError) as exc:
        return None


def credentials_ok():
    """The build refuses the placeholder Wi-Fi details - check first."""
    f = PROJECT / "include" / "wifi_credentials.h"
    try:
        text = f.read_text(encoding="utf-8")
    except OSError as exc:
        return False, "cannot read %s (%s)" % (f, exc)
    if "YourWiFiName" in text or "YourWiFiPassword" in text:
        return False, ("include/wifi_credentials.h still has the placeholder "
                       "Wi-Fi details - fill them in first")
    return True, "credentials look set"


def version():
    try:
        for line in (PROJECT / "include" / "config.h").read_text(
                encoding="utf-8").splitlines():
            if line.startswith("#define BUDDY_VERSION"):
                return line.split('"')[1]
    except OSError:
        pass
    return "?"


def build():
    ok, why = credentials_ok()
    if not ok:
        return {"ok": False, "error": why}
    if not shutil.which(PIO) and not pathlib.Path(PIO).exists():
        return {"ok": False, "error": "PlatformIO not found at %s" % PIO}
    res = _run([PIO, "run"], timeout=1200, cwd=str(PROJECT))
    if res is None:
        return {"ok": False, "error": "the build did not run"}
    tail = (res.stdout or "") + (res.stderr or "")
    ok = res.returncode == 0
    out = {"ok": ok, "version": version(), "returncode": res.returncode}
    for line in tail.splitlines():
        if "RAM:" in line or "Flash:" in line or "error:" in line.lower():
            out.setdefault("notes", []).append(line.strip())
    if not ok:
        out["error"] = tail[-1500:]
    bin_path = PROJECT / ".pio" / "build" / "esp32c3" / "firmware.bin"
    out["firmware"] = str(bin_path)
    out["firmware_bytes"] = (bin_path.stat().st_size
                             if bin_path.exists() else 0)
    return out


def flash_usb():
    res = _run([PIO, "run", "-t", "upload", "--upload-port", SERIAL],
               timeout=900, cwd=str(PROJECT))
    if res is None:
        return {"ok": False, "error": "the upload did not run"}
    tail = (res.stdout or "") + (res.stderr or "")
    return {"ok": res.returncode == 0, "returncode": res.returncode,
            "port": SERIAL,
            "notes": [l.strip() for l in tail.splitlines()
                      if "Flash" in l or "error" in l.lower()][:8]}


def flash_ota():
    """The buddy's own update door: http://<buddy>:8080/update (basic
    auth: user 'deskbuddy', password OTA_PASSWORD)."""
    try:
        import requests
    except ImportError:
        return {"ok": False, "error": "requests is not installed"}
    fw = PROJECT / ".pio" / "build" / "esp32c3" / "firmware.bin"
    if not fw.exists():
        return {"ok": False, "error": "build the firmware first"}
    try:
        cred_file = (PROJECT / "include" / "wifi_credentials.h").read_text(
            encoding="utf-8")
        password = "ChangeMe"
        for line in cred_file.splitlines():
            if "OTA_PASSWORD" in line and '"' in line:
                password = line.split('"')[1]
    except OSError:
        pass
    url = "http://%s:%d/update" % (OTA_HOST, OTA_PORT)
    try:
        with open(fw, "rb") as fh:
            res = requests.post(url, files={"firmware": ("firmware.bin", fh)},
                                auth=("deskbuddy", password), timeout=600)
        return {"ok": res.status_code == 200, "http": res.status_code,
                "url": url, "body": (res.text or "")[:300]}
    except Exception as exc:
        return {"ok": False, "error": str(exc)[:300], "url": url}


def device_status():
    """What the device says about itself (the REST API on port 80)."""
    try:
        with urllib.request.urlopen("http://%s/api/status" % OTA_HOST,
                                    timeout=10) as r:
            st = json.loads(r.read())
        return {"ok": True, "name": st.get("name"), "version": st.get("version"),
                "ip": st.get("ip"), "face": st.get("face"),
                "faceVariant": st.get("faceVariant"), "pack": st.get("pack"),
                "mood": st.get("mood"), "mode": st.get("mode")}
    except Exception as exc:
        return {"ok": False, "error": str(exc)[:200]}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="flash_buddy.py")
    ap.add_argument("command", choices=("build", "flash", "ota", "status"))
    args = ap.parse_args(argv)
    if args.command == "status":
        out = device_status()
    elif args.command == "build":
        out = build()
    elif args.command == "flash":
        out = build()
        if out.get("ok"):
            out["upload"] = flash_usb()
    else:                                   # ota
        out = build()
        if out.get("ok"):
            out["upload"] = flash_ota()
    print(json.dumps(out, indent=2))
    return 0 if out.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
