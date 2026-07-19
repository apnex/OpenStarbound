#!/usr/bin/env bash
#
# The single sanctioned path from this repo to a playable install.
#
# WHY THIS EXISTS. There are two asset paths and they disagree:
#
#   the headless harness reads assets straight from the REPO   (harness/sbinit.config points opensb at
#                                                               /root/frackin/OpenStarbound/assets/opensb)
#   the live game reads ONLY the loose override in the install (dev/opensb, dev-feat/opensb)
#
# So a repo-only asset edit is live in the harness and a SILENT NO-OP in the game. The harness can be
# testing different shaders than the ones you are playing, and nothing tells you. On 2026-07-14 a
# half-completed deploy shipped a new binary against stale #version 140 shaders and was caught only by
# manually re-checking -- hence this script, and hence --verify.
#
# It also defends against the two ways the manual deploy actually failed that day:
#   * `cp` is aliased to `cp -i` here, so a plain `cp -f` PROMPTS and then silently skips every file.
#     We use `command cp` to bypass the alias.
#   * `rsync --delete` would wipe install-local files that are not in the repo. We never delete.
#
# USAGE
#   scripts/deploy-install.sh <install-dir> [--verify] [--force]
#     <install-dir>   e.g. /home/apnex/OpenStarbound/dev  or .../dev-feat
#     --verify        deploy nothing; just prove the install matches the repo. Exit 1 on any drift.
#     --force         deploy even from a dirty or mid-merge tree (default: refuse)
#
# EXIT: 0 = install matches the repo. 1 = drift, or a refused deploy.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BIN_SRC="$REPO/dist/starbound"
ASSET_SRC="$REPO/assets/opensb"

INSTALL=""
VERIFY_ONLY=0
FORCE=0

for arg in "$@"; do
  case "$arg" in
    --verify) VERIFY_ONLY=1 ;;
    --force)  FORCE=1 ;;
    -*)       echo "unknown flag: $arg" >&2; exit 1 ;;
    *)        INSTALL="$arg" ;;
  esac
done

if [[ -z "$INSTALL" ]]; then
  echo "usage: $0 <install-dir> [--verify] [--force]" >&2
  exit 1
fi
if [[ ! -d "$INSTALL/opensb" ]]; then
  echo "FAIL: '$INSTALL' does not look like an install (no opensb/ overlay)" >&2
  exit 1
fi

# ---------------------------------------------------------------- verify

# Compare every file the repo believes it owns against the install. Reports drift in BOTH directions that
# matters: a repo file missing or differing in the install. (Install-local extras are fine and never touched.)
verify() {
  local drift=0 checked=0

  if [[ ! -f "$BIN_SRC" ]]; then
    echo "  FAIL: no built binary at dist/starbound -- build first" >&2
    return 1
  fi
  if ! cmp -s "$BIN_SRC" "$INSTALL/starbound"; then
    echo "  DRIFT  starbound            repo=$(md5sum "$BIN_SRC" | cut -c1-12)  install=$(md5sum "$INSTALL/starbound" 2>/dev/null | cut -c1-12 || echo MISSING)"
    drift=1
  fi

  while IFS= read -r -d '' src; do
    local rel="${src#$ASSET_SRC/}"
    local dst="$INSTALL/opensb/$rel"
    checked=$((checked + 1))
    if [[ ! -f "$dst" ]]; then
      echo "  DRIFT  opensb/$rel  (MISSING in install)"
      drift=1
    elif ! cmp -s "$src" "$dst"; then
      echo "  DRIFT  opensb/$rel  (differs)"
      drift=1
    fi
  done < <(find "$ASSET_SRC" -type f -print0)

  if [[ $drift -eq 0 ]]; then
    echo "  OK: binary + $checked asset files match the repo"
    return 0
  fi
  return 1
}

if [[ $VERIFY_ONLY -eq 1 ]]; then
  echo "VERIFY $INSTALL against $REPO"
  verify
  exit $?
fi

# ---------------------------------------------------------------- deploy

# Refuse to deploy from a tree that is not a coherent statement of intent. A mid-merge or dirty tree means
# the binary in dist/ and the assets on disk may not be the same revision -- exactly the drift this exists
# to prevent.
cd "$REPO"
if [[ $FORCE -eq 0 ]]; then
  if [[ -f .git/MERGE_HEAD ]]; then
    echo "REFUSED: tree is mid-merge (.git/MERGE_HEAD present). Finish the merge, or pass --force." >&2
    exit 1
  fi
  if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "REFUSED: tree is dirty -- the binary in dist/ may not match the assets on disk." >&2
    git status --short | sed 's/^/    /' >&2
    echo "  Commit, stash, or pass --force." >&2
    exit 1
  fi
fi

if [[ ! -f "$BIN_SRC" ]]; then
  echo "FAIL: no built binary at dist/starbound -- build first" >&2
  exit 1
fi

echo "DEPLOY $REPO ($(git rev-parse --short HEAD)) -> $INSTALL"

# `command cp` bypasses the interactive `cp -i` alias, which otherwise prompts and silently skips.
command cp -f "$BIN_SRC" "$INSTALL/starbound"
n=0
while IFS= read -r -d '' src; do
  rel="${src#$ASSET_SRC/}"
  dst="$INSTALL/opensb/$rel"
  mkdir -p "$(dirname "$dst")"
  command cp -f "$src" "$dst"
  n=$((n + 1))
done < <(find "$ASSET_SRC" -type f -print0)

owner="$(stat -c '%U:%G' "$INSTALL")"
chown -R "$owner" "$INSTALL/starbound" "$INSTALL/opensb" 2>/dev/null || true

echo "  copied binary + $n asset files"

# Never trust the copy -- prove it. This is the whole point.
echo "VERIFY"
verify
