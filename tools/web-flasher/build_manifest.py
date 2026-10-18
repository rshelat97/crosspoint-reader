#!/usr/bin/env python3
"""Write manifest.json for the web flasher from downloaded release assets.

Usage: build_manifest.py <firmware-dir> <release-tag>

Reads crosspoint-<version>-<device>.bin files (the names release.yml uploads)
and records device, size and SHA-256 so the page can verify its download.
An empty directory produces an empty asset list (no release published yet).
"""
import hashlib
import json
import re
import sys
from pathlib import Path


def main() -> int:
    fw_dir = Path(sys.argv[1])
    tag = sys.argv[2] if len(sys.argv) > 2 else ""
    version = tag.removeprefix("v")
    pattern = re.compile(rf"^crosspoint-{re.escape(version)}-(.+)\.bin$")
    assets = []
    for path in sorted(fw_dir.glob("crosspoint-*.bin")):
        match = pattern.match(path.name)
        if not match:
            print(f"skipping unexpected asset {path.name}", file=sys.stderr)
            continue
        data = path.read_bytes()
        assets.append(
            {
                "device": match.group(1),
                "file": path.name,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    (fw_dir / "manifest.json").write_text(json.dumps({"tag": tag, "assets": assets}, indent=2) + "\n")
    print(f"manifest: {tag or '(no release)'} with {len(assets)} assets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
