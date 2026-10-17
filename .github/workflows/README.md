# Lotus CI workflows

Two GitHub Actions workflows: `ci.yml` validates pull requests, `build.yml` builds and
releases tagged versions.

The build is a **reproduction of the project's own build**, not a build tuned to the CI.
That is the rule everything below serves: when a build fails, change the build, not the
tree. No `depends/` pin moves, no source workaround is added, and no gate is relaxed to get
a run green.

## The build environment

TeamCity builds this tree on Debian buster with GCC 8.3 and glibc 2.28
(`contrib/teamcity/setup-debian-buster.sh`), and `depends/`'s pins were validated against
that toolchain. The container images follow: `debian:buster` builder stages whose packages
come from `contrib/utils/install-dependencies.sh` — the same script the TeamCity agent runs —
rather than a hand-maintained `apt-get` list. clang-10 is the default compiler for clang jobs
(`setup-debian-buster.sh:20-22`); `depends` itself builds with `$(host)-gcc`.

Two details look like local quirks and are not: buster is EOL, so base sources are rewritten
to `archive.debian.org` and the `Check-Valid-Until` override is written *before*
`install-dependencies.sh` runs, because the script does not set it until line 99, after its
own `apt-get update` at line 90.

### Per-host mapping

| Workflow job | `depends` HOST | Toolchain file |
|---|---|---|
| `build-linux` (x86_64) | `x86_64-linux-gnu` | `cmake/platforms/Linux64.cmake` |
| `build-linux` (armhf) | `arm-linux-gnueabihf` | `cmake/platforms/LinuxARM.cmake` |
| `build-linux` (aarch64) | `aarch64-linux-gnu` | `cmake/platforms/LinuxAArch64.cmake` |
| `build-win64` | `x86_64-w64-mingw32` | `cmake/platforms/Win64.cmake` |
| `build-osx` | `x86_64-apple-darwin16` | `cmake/platforms/OSX.cmake` |

This is gitian's matrix (`contrib/gitian-descriptors/gitian-linux.yml:42-46` plus the
osx/win descriptors). linux32 is built by TeamCity but never released, so it has no row.

### How `depends` is invoked, and why it matters

```
make -C depends HOST=<triple> \
  CMAKE_TOOLCHAIN_FILE=<repo>/cmake/platforms/<X>.cmake \
  BASEPREFIX=<repo>/depends
```

`CMAKE_TOOLCHAIN_FILE` is load-bearing: `depends/packages/nng.mk:18` and
`Flatbuffers.mk:20` pass `-DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TOOLCHAIN_FILE)` to CMake, and
nothing in `depends/` defines that variable. Omitted, CMake discards the empty argument and
compiles those packages with the build machine's compiler — invisible natively, and fatal
for a cross target (`Relocations in generic ELF (EM: 62)`, or a 64-bit nng config rejected
for armhf). Gitian passes it on the command line; so do we.

Qt is 5.9.8 and there is no `NO_QT`. Every image builds the dependency set the release
build builds, which includes the GUI for the Linux rows.

The Python packages needed to *run* the test suites (`pytest`, `pynng`, `flatbuffers`) are
installed by `contrib/utils/install-python-test-deps.sh`, and Node.js for the cashtab frontend
jobs by `contrib/utils/install-nodejs.sh`. The TeamCity agent calls both from
`setup-debian-buster.sh`; build images call neither, because compiling and packaging lotusd
needs no Python test framework and no Node. That split also keeps the image off buster's pip
18.1, which predates `manylinux2014` and so turns any modern wheel into a broken source build.

`ninja symbol-check` runs on the buster build, exactly as `gitian-linux.yml:167-168` runs
it, and its `GLIBC` ceiling is calibrated to that base. A newer base image does not merely
build differently — it fails that gate on symbols no source change can remove.

## `ci.yml` — pull-request validation

Triggers on `pull_request` against `master`. It does not build; it lints.

- **`lint`** — project linters scoped to the files the PR changed:
  yamllint, `sh -n`, `py_compile` + `lint-python-format.py`, `lint-format-strings.py`,
  a diff-scoped trailing-whitespace check, and the two project-wide invariants
  (`lint-circular-dependencies.sh`, `lint-tests.sh`).
- **`dockerfile-check`** — `docker buildx build --check` on each Dockerfile in
  `dockerfiles/`, without building.

## `build.yml` — tag-driven build and release

Triggers on a tag matching `v*.*.*`, or manually via `workflow_dispatch` (required
`version` input).

| Job | Platform | Artifacts |
|---|---|---|
| `build-linux` | x86_64 / armhf / aarch64 (matrix) | core binaries, `lotus-qt`, debug symbols |
| `build-win64` | Windows x86_64 | binaries, NSIS installer, debug symbols |
| `build-osx` | macOS x86_64 | binaries, unsigned `.dmg` |
| `test` | Linux x86_64 | functional tests; gates the release |
| `release` | — | aggregates artifacts, `SHA256SUMS`, source tarball, publishes |

Each build job cross-compiles in its Dockerfile, then extracts and packages the binaries.

### Publishing behaviour

- **Tag push** (`v*.*.*`): images are pushed to `ghcr.io` and the release is **published**.
- **Manual `workflow_dispatch`**: images are built and loaded locally, not pushed, and the
  release is created as a **draft**. A manual run publishes nothing; it smoke-tests the
  pipeline. The release job `needs:` every build, so it is skipped while any build is red.

See `RELEASE.md` at the repository root for the `make release` flow that drives this.

## Changing this CI

- A red build is a build problem until proven otherwise. Check the failure against the
  stage it reached before touching `src/` or a pin.
- Any new or changed CMake-based `depends` recipe must be given the toolchain file; the
  empty-variable failure above is silent at configure time and only shows up at link or
  in a package's config version check.
- Adding a target is three checks, not one: the triple must parse in `depends/config.sub`,
  a toolchain file must exist in `cmake/platforms/`, and the pinned Qt must have a mkspec
  for it. macOS arm64 and Windows arm64 fail the latter two today.
