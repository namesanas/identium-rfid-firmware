"""Restore one of this ESP32's saved full flash images using esptool v5."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
IMAGE_DIR = ROOT / "firmware_images"


def esptool_python(override=None):
    candidates = []
    if override:
        candidates.append(Path(override))
    else:
        if importlib.util.find_spec("esptool") is not None:
            candidates.append(Path(sys.executable))
        env_path = os.environ.get("IDF_PYTHON_ENV_PATH")
        if env_path:
            candidates.append(Path(env_path) / "Scripts" / "python.exe")
        candidates.extend(
            sorted(Path("C:/Espressif/tools/python").glob("*/venv/Scripts/python.exe"),
                   reverse=True)
        )
    for candidate in candidates:
        if not candidate.is_file():
            continue
        result = subprocess.run(
            [str(candidate), "-c", "import esptool; print(esptool.__version__)"],
            capture_output=True, text=True,
        )
        if result.returncode == 0 and result.stdout.strip().startswith("5."):
            return str(candidate)
    raise ValueError("esptool v5 not found. Use --python PATH to its Python interpreter.")


def checked_image(manifest, choice):
    if (manifest.get("schema_version") != 1 or manifest.get("chip") != "esp32"
            or manifest.get("flash_bytes") != 0x400000):
        raise ValueError("Expected a classic ESP32 manifest with 4 MB images.")
    item = manifest["images"][choice]
    path = (IMAGE_DIR / item["file"]).resolve()
    if path.parent != IMAGE_DIR.resolve():
        raise ValueError("Image must be inside firmware_images.")
    data = path.read_bytes()
    if len(data) != manifest["flash_bytes"]:
        raise ValueError(f"Incomplete flash image: {path.name}")
    if hashlib.sha256(data).hexdigest() != item["sha256"]:
        raise ValueError(f"SHA256 mismatch: {path.name}")
    app_offset = item["app_offset"]
    if data[0x1000] != 0xE9 or data[app_offset] != 0xE9:
        raise ValueError("ESP32 bootloader or application header is missing.")
    if struct.unpack_from("<I", data, app_offset + 0x20)[0] != 0xABCD5432:
        raise ValueError("ESP-IDF application descriptor is missing.")
    project = data[app_offset + 0x50:app_offset + 0x70].split(b"\0", 1)[0].decode()
    if project != item["project"]:
        raise ValueError(f"Unexpected application: {project}")
    return item, path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", choices=("wifi", "handheld"))
    parser.add_argument("--port", default="COM10")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--python", help="Python interpreter containing esptool v5")
    parser.add_argument("--dry-run", action="store_true", help="Check images without opening a port")
    args = parser.parse_args()

    try:
        manifest = json.loads((IMAGE_DIR / "manifest.json").read_text(encoding="utf-8"))
        choice = args.firmware
        if choice is None:
            print(f"ESP32 firmware switcher - {args.port}")
            print("Disconnect STM32 PA15 -> ESP32 EN; leave ESP32 USB connected.")
            print("Close any serial monitor using this port.")
            print("Each choice restores a full image, including its saved settings.")
            print("1. wifi_coprocessor")
            print("2. mubr_handheld v0.7.1")
            print("0. Exit")
            answer = input("Choose 1, 2 or 0: ").strip()
            if answer == "0":
                return 0
            choice = {"1": "wifi", "2": "handheld"}.get(answer)
            if choice is None:
                raise ValueError("Choose 1, 2 or 0.")

        item, path = checked_image(manifest, choice)
        python = esptool_python(args.python)
        command = [
            python, "-m", "esptool", "--chip", "esp32", "--port", args.port,
            "--baud", str(args.baud), "--after", "hard-reset", "write-flash",
            "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep",
            "0x0", str(path),
        ]
        print(f"Image checked: {item['project']} {item['version']}", flush=True)
        if args.dry_run:
            print(subprocess.list2cmdline(command))
            return 0
        print("Keep PA15 -> EN disconnected until flashing finishes.", flush=True)
        result = subprocess.run(command)
        if result.returncode:
            print("Flash failed. Keep the jumper disconnected and retry. If connection fails,\n"
                  "hold BOOT, tap EN, release BOOT, then run this tool again.", file=sys.stderr)
            return result.returncode
        print("Flash verified by esptool; ESP32 restarted.")
        if choice == "wifi":
            print("Reconnect PA15 -> EN, then reset the STM32 to start the coprocessor link.")
        else:
            print("Leave PA15 -> EN disconnected while using the handheld firmware.")
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    except (EOFError, KeyboardInterrupt):
        print("\nCancelled.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
