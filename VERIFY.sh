#!/usr/bin/env bash
set -euo pipefail
f=${1:-OmniGuess-macos-universal}
lipo -info "$f"
if command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$f" | tee SHA256SUMS
elif command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$f" | tee SHA256SUMS
else
  echo "No shasum/sha256sum found" >&2
  exit 1
fi
