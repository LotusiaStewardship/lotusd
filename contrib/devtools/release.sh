#!/usr/bin/env bash
#
# Cut a release: bump the tree version, commit it, tag, and push the tag.
#
# This is the tracked implementation of the release procedure. The root `Makefile` is not in
# version control (`.gitignore` carries a `Makefile` rule from the autotools era), so `make
# release` is only a local convenience wrapper around this script -- `contrib/devtools/release.sh`
# is what a fresh clone has and what CI's invariants are written against.
#
# The tag must equal the tree's version: `CMakeLists.txt`'s `project(VERSION ...)` drives
# CLIENT_VERSION_*, the binaries' --version output and CPack's installer name, while the tag
# drives the release name and every artifact filename. This script keeps them in step by
# construction, and `.github/workflows/build.yml` refuses to build a release where they differ.
#
# Only the tag created here is pushed. `git push --tags` would publish every local tag,
# including the archive/* and backup/* refs that exist in this repository.

export LC_ALL=C

set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  contrib/devtools/release.sh --bump patch|minor|major
  contrib/devtools/release.sh --version x.y.z

Bumps the version in CMakeLists.txt and both AUR PKGBUILDs, commits it as
"Bump version to X.Y.Z", creates tag vX.Y.Z and pushes both to origin/master.

Preconditions: on master, no uncommitted changes to tracked files.
EOF
}

bump=""
version_arg=""

while [ $# -gt 0 ]; do
    case "$1" in
        --bump)    bump="${2:?--bump needs a value}"; shift 2 ;;
        --version) version_arg="${2:?--version needs a value}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 1 ;;
    esac
done

branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$branch" != "master" ]; then
    echo "error: releases are cut from master, not '$branch'" >&2
    exit 1
fi

if ! git diff --quiet; then
    echo "error: uncommitted changes to tracked files; commit or stash them first" >&2
    exit 1
fi

if ! git diff --cached --quiet; then
    echo "error: staged but uncommitted changes; commit them first" >&2
    exit 1
fi

current=$(awk '/^[[:space:]]*VERSION/{print $2; exit}' CMakeLists.txt)
if [ -z "$current" ]; then
    echo "error: could not read version from CMakeLists.txt" >&2
    exit 1
fi

if [ -n "$version_arg" ]; then
    if ! echo "$version_arg" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        echo "error: --version must be x.y.z (numeric only)" >&2
        exit 1
    fi
    new="$version_arg"
elif [ -n "$bump" ]; then
    IFS=. read -r major minor patch <<< "$current"
    case "$bump" in
        major) new="$((major + 1)).0.0" ;;
        minor) new="$major.$((minor + 1)).0" ;;
        patch) new="$major.$minor.$((patch + 1))" ;;
        *) echo "error: --bump must be major, minor or patch" >&2; exit 1 ;;
    esac
else
    echo "error: specify --bump patch|minor|major or --version x.y.z" >&2
    usage >&2
    exit 1
fi

if [ "$current" = "$new" ]; then
    echo "error: version unchanged ($current)" >&2
    exit 1
fi

# Checked before anything is written: `git tag` would otherwise abort *after* the bump commit
# landed, leaving master with an unpushed version commit, no tag, and a tree the script refuses
# to bump again ("version unchanged").
if git rev-parse --verify --quiet "refs/tags/v$new" >/dev/null; then
    echo "error: tag v$new already exists locally" >&2
    exit 1
fi
if [ -n "$(git ls-remote --tags origin "refs/tags/v$new" 2>/dev/null)" ]; then
    echo "error: tag v$new already exists on origin" >&2
    exit 1
fi

echo "Bumping version: $current -> $new"

# -i.bak works on both GNU sed and BSD sed; the backup is removed on success.
sed -i.bak "s/^\\([[:space:]]*\\)VERSION $current\$/\\1VERSION $new/" CMakeLists.txt
rm -f CMakeLists.txt.bak
tree_version=$(awk '/^[[:space:]]*VERSION/{print $2; exit}' CMakeLists.txt)
if [ "$tree_version" != "$new" ]; then
    echo "error: CMakeLists.txt declares '$tree_version' after the bump, expected '$new'" >&2
    exit 1
fi

# Both AUR packages pin their source tarball by tag (`archive/v$pkgver.tar.gz`), so a PKGBUILD
# left behind ships an older tree: for lotus-qt that is a node without the current consensus
# rules. pkgver is therefore set from $new rather than $current, so a PKGBUILD that drifted out
# of sync catches up here instead of silently keeping its version. pkgrel counts re-packages of
# the same pkgver and resets on every version bump (contrib/aur/README.md).
for pkgbuild in contrib/aur/lotus/PKGBUILD contrib/aur/lotus-qt/PKGBUILD; do
    if [ ! -f "$pkgbuild" ]; then
        echo "error: $pkgbuild is missing" >&2
        exit 1
    fi
    was=$(sed -n 's/^pkgver=//p' "$pkgbuild")
    sed -i.bak -e "s/^pkgver=.*/pkgver=$new/" -e "s/^pkgrel=.*/pkgrel=0/" "$pkgbuild"
    rm -f "$pkgbuild.bak"
    now=$(sed -n 's/^pkgver=//p' "$pkgbuild")
    if [ "$now" != "$new" ]; then
        echo "error: $pkgbuild declares pkgver='$now' after the bump, expected '$new'" >&2
        exit 1
    fi
    if [ "$was" != "$current" ]; then
        echo "note: $pkgbuild was at '$was', catching it up to $new" >&2
    fi
done

# -u stages the tracked copies only. `.gitignore` carries a bare `lotus-qt` rule from the
# autotools era that also matches contrib/aur/lotus-qt/, and a plain `git add` on that path
# exits non-zero even though the file is tracked -- which, under `set -e`, would leave the tree
# bumped but uncommitted.
git add -u CMakeLists.txt contrib/aur/lotus/PKGBUILD contrib/aur/lotus-qt/PKGBUILD
git commit -m "Bump version to $new"
git tag "v$new"

echo "Pushing commit and tag v$new..."
git push origin master "v$new"

echo "Done. GitHub Actions builds v$new and creates its release."
