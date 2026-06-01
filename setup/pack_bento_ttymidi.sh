#!/bin/bash
# Build bento_ttymidi and stage binary + systemd unit into dist/ for integration.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v make >/dev/null 2>&1; then
    echo "[ERROR] make not found. Install build-essential."
    exit 1
fi

if ! pkg-config --exists alsa 2>/dev/null; then
    echo "[WARN] libasound2-dev may be missing (pkg-config alsa not found)"
fi

if [ ! -f packaging/bento_ttymidi.service ]; then
    echo "[ERROR] packaging/bento_ttymidi.service not found"
    exit 1
fi

echo "Building bento_ttymidi..."
make clean 2>/dev/null || true
make

echo "Staging dist/..."
rm -rf dist
mkdir -p dist
cp -f bento_ttymidi dist/bento_ttymidi
chmod 755 dist/bento_ttymidi
cp -f packaging/bento_ttymidi.service dist/bento_ttymidi.service
cp -f packaging/dist.README.md dist/README.md

if git rev-parse --short HEAD >/dev/null 2>&1; then
    git rev-parse --short HEAD >dist/VERSION
    echo "Version: $(cat dist/VERSION)"
fi

echo ""
echo "Pack complete: $ROOT/dist/"
ls -la dist/
echo ""
echo "For integration: copy dist/* into your build file area, then install per dist/README.md"
