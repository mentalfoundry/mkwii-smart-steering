#!/usr/bin/env bash
# Build a release and zip it for GitHub Releases. Run from Git Bash.
#
#   ./package.sh 1.0.0    -> release/MKWiiSmartSteering-1.0.0.zip
#
# The zip holds the contents of dist/ (the Riivolution XML and the mod
# folder) plus README and license files, laid out so players can extract it
# straight onto their SD card or into Dolphin's Load/Riivolution folder.
set -euo pipefail
cd "$(dirname "$0")"

VERSION=${1:-}
if [ -z "$VERSION" ]; then
    echo "usage: ./package.sh <version>   e.g. ./package.sh 1.0.0" >&2
    exit 1
fi

./build.sh

mkdir -p release
ZIP="release/MKWiiSmartSteering-$VERSION.zip"
rm -f "$ZIP"

python - "$ZIP" <<'EOF'
import os, sys, zipfile
zip_path = sys.argv[1]
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk("dist"):
        for name in files:
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, "dist"))
    z.write("README.md", "MKWiiSmartSteering/README.md")
    z.write("LICENSE", "MKWiiSmartSteering/LICENSE")
    for name in sorted(os.listdir("third_party_licenses")):
        z.write(os.path.join("third_party_licenses", name), "MKWiiSmartSteering/third_party_licenses/" + name)
    for info in z.infolist():
        print("  " + info.filename)
EOF

echo "Created $ZIP"
