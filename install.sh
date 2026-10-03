#!/usr/bin/env bash
# Fin compiler installer: clones M1778/Fin and builds it with ./build.sh.
# Usage: curl -fsSL https://raw.githubusercontent.com/M1778/Fin/master/install.sh | sh
set -euo pipefail

REPO="https://github.com/M1778/Fin"
DIR="${FIN_DIR:-$HOME/Fin}"

command -v git >/dev/null || { echo "install.sh: git is required" >&2; exit 1; }

if [ -d "$DIR/.git" ]; then
  git -C "$DIR" pull --ff-only
else
  git clone --depth 1 "$REPO" "$DIR"
fi

cd "$DIR"
./build.sh --release --no-test
echo "finc ready at: $DIR/build/finc"
