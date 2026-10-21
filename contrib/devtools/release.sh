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

Bumps the version in CMakeLists.txt and contrib/aur/lotus/PKGBUILD, commits it as
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

echo "Bumping version: $current -> $new"

# -i.bak works on both GNU sed and BSD sed; the backup is removed on success.
sed -i.bak "s/^\\([[:space:]]*\\)VERSION $current\$/\\1VERSION $new/" CMakeLists.txt
rm -f CMakeLists.txt.bak
sed -i.bak "s/pkgver=$current/pkgver=$new/" contrib/aur/lotus/PKGBUILD
rm -f contrib/aur/lotus/PKGBUILD.bak

git add CMakeLists.txt contrib/aur/lotus/PKGBUILD
git commit -m "Bump version to $new"
git tag "v$new"

echo "Pushing commit and tag v$new..."
git push origin master "v$new"

echo "Done. GitHub Actions builds v$new and creates its release."
