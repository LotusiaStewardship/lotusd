# Release Process

Releases are cut from `master` by a tracked script:

```bash
contrib/devtools/release.sh --bump patch      # 11.5.1 -> 11.5.2
contrib/devtools/release.sh --bump minor      # 11.5.1 -> 11.6.0
contrib/devtools/release.sh --bump major      # 11.5.1 -> 12.0.0
contrib/devtools/release.sh --version 11.5.2  # explicit
```

`make release bump=patch` and `make release VERSION=11.5.2` still work as a local convenience,
but the root `Makefile` is **not in version control** (`.gitignore` carries a `Makefile` rule
from the autotools era), so the script is the procedure — it is what a fresh clone has.

## Prerequisites

- On `master`. The script refuses to run on any other branch: a release cut from a feature
  branch would tag a commit that never reached `master`.
- No uncommitted changes to tracked files. Untracked notes and plans are fine.
- `origin` points at the canonical remote.
- Write access to `origin`.

## What the script does

1. Reads the current version from `CMakeLists.txt` (the `VERSION` field in `project()`).
2. Bumps it, or applies `--version`.
3. Updates `CMakeLists.txt` and `contrib/aur/lotus/PKGBUILD`.
4. Commits as `Bump version to X.Y.Z`.
5. Tags that commit as `vX.Y.Z`.
6. Pushes `master` and **only** that tag to `origin`. It does not use `--tags`, which would
   publish every local tag — this repository carries `archive/*` and `backup/*` refs that must
   never be pushed.

## Version rules

Versions are `x.y.z`, numeric only. `project(VERSION ...)` in `CMakeLists.txt` rejects suffixes,
so `11.6.0-rc1` is not a valid release version.

The tag and the tree must agree. `project(VERSION ...)` drives `CLIENT_VERSION_*`, the binaries'
`--version` output, the Windows resource and CPack's installer name, while the tag drives the
release name and every artifact filename. `build.yml` refuses to build a release where they
differ, so hand-tagging a tree that was not bumped fails instead of shipping binaries that
misreport their version.

## What CI does with the tag

Pushing `vX.Y.Z` triggers `.github/workflows/build.yml` (`on: push: tags: ["v*.*.*"]`), which:

- builds five platforms in Docker — linux x86_64 / armhf / aarch64 (daemon, CLI and GUI), macOS
  x86_64, Windows x86_64 — from the same dependency set TeamCity uses (Debian buster,
  `contrib/utils/install-dependencies.sh`, Qt 5.9.8, OpenSSL 1.0.1k, boost 1.70);
- packages linux tarballs plus debug symbols, a macOS `.dmg`, and a Windows zip plus NSIS
  installer;
- runs the functional test suite against the linux x86_64 binaries;
- requires the full 13-file asset manifest before creating the release;
- generates notes for exactly `previous-release..tag`;
- publishes the release and pushes images to `ghcr.io`.

## Testing the pipeline without cutting a release

A `workflow_dispatch` run is a production build that is not published: it produces a **draft**
release and, because drafts do not create refs until published, **no tag**.

To rehearse: bump the version on a branch (`contrib/devtools/release.sh` refuses to run there,
so edit `CMakeLists.txt` directly), push the branch, and dispatch with that same version. The tag
does not exist yet, so the draft has no ref and deleting it leaves nothing behind.

Do not dispatch with the version of an existing tag — the draft would attach to that tag, and
publishing it by accident would take over that release.

## If a build fails

The tag is already published, so the release simply does not appear. Re-run the workflow for that
tag after fixing the problem, or delete the tag (`git push origin :vX.Y.Z`) and cut it again. A
tag on a commit that is not on `master` is never a valid release.
