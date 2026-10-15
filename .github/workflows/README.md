# Lotus CI Workflow System

Two GitHub Actions workflows handle CI for Lotus: `ci.yml` validates pull
requests, and `build.yml` builds and releases tagged versions.

## `ci.yml` — pull-request validation

Triggers on `pull_request` against `master`. It **does not build** any binaries;
it only lints.

Two jobs:

- **`lint`** — runs the project's linters, scoped to the files changed in the PR
  (diff against the PR base, not the whole tree):
  - YAML lint (`yamllint`)
  - shell syntax (`sh -n`)
  - Python syntax (`py_compile`) and `%`-formatter lint (`lint-python-format.py`)
  - C++ format-string lint (`lint-format-strings.py`)
  - trailing-whitespace check on the PR diff
  - two project-wide invariants: circular-dependency check and test-suite naming
    check (`test/lint/`)

- **`dockerfile-check`** — validates the syntax of all four Dockerfiles
  (`docker buildx build --check`) without building.

## `build.yml` — tag-driven build and release

Triggers on a pushed tag matching `v*.*.*`, or manually via
`workflow_dispatch` (with a `version` input). Five jobs:

| Job | Platform | Artifacts |
|---|---|---|
| `build-linux` | x86_64 / arm / aarch64 (matrix) | core binaries + debug symbols |
| `build-win64` | Windows x86_64 | binaries, NSIS installer, debug symbols |
| `build-osx` | macOS x86_64 | binaries, unsigned `.dmg` |
| `build-qt` | Linux x86_64 GUI | `lotus-qt` |
| `release` | — | aggregates all artifacts, generates `SHA256SUMS` + source tarball, publishes the release |

Each build job cross-compiles inside a Docker container via the corresponding
`dockerfiles/Dockerfile.lotus*`, extracts the binaries, and packages them.

### Publishing behaviour

- **Tag push** (`v*.*.*`): images are pushed to `ghcr.io`, and the release job
  **publishes** the GitHub release.
- **Manual `workflow_dispatch`**: images are built and loaded locally (`load`),
  **not pushed to `ghcr.io`**, and the release is created as a **draft** only.
  Manual runs publish nothing — they exist to smoke-test the pipeline.

See `RELEASE.md` at the repository root for the `make release` flow that drives
this via tag pushes.
