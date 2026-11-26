"""Package the profile-enabled app separately, retaining all earlier binaries."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import re
from datetime import datetime, timezone

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--variant", default="device-profiles",
                    help="Separate package/build folder name; existing default is preserved.")
args = parser.parse_args()
if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", args.variant):
    parser.error("--variant must contain only lowercase letters, digits and hyphens (1..64 characters)")
root = Path(__file__).resolve().parents[1]
dist = root / "dist" / args.variant
work = root / "build" / args.variant
log = root / "build" / (args.variant + "-build.log")
dist.mkdir(parents=True, exist_ok=True)
work.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
env["PYINSTALLER_CONFIG_DIR"] = str(root / "build" / "pyinstaller-cache")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


with log.open("w", encoding="utf-8") as output:
    result = subprocess.run([
        sys.executable, "-m", "PyInstaller", "--noconfirm",
        "--distpath", str(dist), "--workpath", str(work), "main.spec",
    ], cwd=root, env=env, stdout=output, stderr=subprocess.STDOUT, check=False)
if result.returncode:
    print("Build failed:", log)
    raise SystemExit(result.returncode)
exe = dist / "TAGlink.exe"
source_files = sorted(list(root.glob("*.py")) + [root / "main.spec", root / "requirements.txt"])
manifest = {
    "built_at_utc": datetime.now(timezone.utc).isoformat(),
    "feature": "IF9D/MUBR startup profiles and MUBR Bluetooth LE",
    "file": "TAGlink.exe",
    "bytes": exe.stat().st_size,
    "sha256": digest(exe),
    "python": sys.version,
    "sources": {str(path.relative_to(root)): digest(path) for path in source_files},
    "log": str(log.relative_to(root)),
    "hardware_validation": "Not performed by this build helper.",
}
(dist / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(json.dumps({key: manifest[key] for key in ("file", "bytes", "sha256", "log")}, indent=2))
