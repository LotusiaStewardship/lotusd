# Release Process

The root `Makefile` provides a `release` target that bumps the version,
commits, tags, and pushes a new release.

## Prerequisites

- Clean working tree (no uncommitted changes)
- Write access to `origin`
- `origin` points to the canonical remote

## Usage

### Auto-bump

```bash
make release bump=patch    # 11.4.15 -> 11.4.16  (bugfix)
make release bump=minor    # 11.4.15 -> 11.5.0   (feature)
make release bump=major    # 11.4.15 -> 12.0.0   (breaking)
```

### Explicit version

```bash
make release VERSION=11.4.16
```

Version must be `x.y.z` (numeric only, no suffixes like `-rc1`).

## What it does

1. Reads the current version from `CMakeLists.txt` (the `VERSION` field in
   `project()`).
2. Bumps or applies the given version.
3. Updates `CMakeLists.txt` and `contrib/aur/lotus/PKGBUILD`.
4. Commits as `Bump version to X.Y.Z`.
5. Tags as `vX.Y.Z`.
6. Pushes the commit and tag to `origin master --tags`.

## Safety

- Refuses to run if the version hasn't changed.
- Refuses to run if `VERSION` doesn't match `x.y.z`.
- Refuses to run if `bump` isn't one of `patch`, `minor`, `major`.
- Creates a `.bak` backup during sed and removes it on success.

## How the Makefile interfaces with CI

The Makefile tags the release as `vX.Y.Z` and pushes it to `origin master --tags`.
This push triggers the **Build** workflow (`.github/workflows/build.yml`) via its
`on: push: tags: ["v*.*.*"]` trigger.

The Build workflow then:
- Builds Docker images (`lotus` and `lotus-qt`) and pushes them to `ghcr.io`
- Extracts binaries from the Docker images and packages them as tarballs
- Generates release notes from the git log between the previous and current tag
- Creates a GitHub Release with the tarballs attached

You can also trigger the Build workflow manually from the GitHub UI
(`workflow_dispatch` with a version input) without using the Makefile.

## Manual alternative

If you need pre-release suffixes or want to skip the push:

```bash
# Bump manually, commit, and tag
sed -i '' 's/VERSION 11\.4\.15/VERSION 11.5.0-rc1/' CMakeLists.txt
git add CMakeLists.txt
git commit -m "Bump version to 11.5.0-rc1"
git tag v11.5.0-rc1

# Push when ready
git push origin master --tags
```
