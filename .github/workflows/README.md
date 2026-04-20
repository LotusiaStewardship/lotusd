# Lotus CI Workflow System

Modular GitHub Actions pipeline for the `LotusDevOrg/lotusd` fork. Designed
to supersede the upstream `LotusiaStewardship/lotusd` workflows with full
multi-arch and multi-OS coverage.

## Trigger model

| Event | What runs |
| --- | --- |
| Push of a version tag (`XX.YY.ZZ` or `vXX.YY.ZZ`) | Full release pipeline: Linux + Windows + macOS + Docker manifests + GPU miner + GitHub release. |
| Push to **any** branch (`master`, `main`, `dev`, feature branches, …) | **Linux-only** dev build. Artifacts uploaded as workflow artifacts. No tag, no GitHub release, no Docker push. |
| Pull request | Same as a branch push: Linux-only dev build. |
| Manual `workflow_dispatch` | Linux-only by default; tick the **`full_pipeline`** input to run the release-grade pipeline. |

Releases are cut by manually editing `CMakeLists.txt` +
`contrib/aur/lotus/PKGBUILD` to the new version and pushing a git tag of
the same value (e.g. `git tag 11.0.1 && git push origin 11.0.1`). The tag
value is the canonical version for artifacts, Docker tags and the GitHub
release page.

## Workflow layout

### Orchestrator

- **[lotus-main-ci.yml](./lotus-main-ci.yml)** — single entry point. A
  `classify` job decides at runtime whether the trigger is a release
  (version tag / forced dispatch) or a dev build (anything else) and
  conditionally fans out to the build workflows.

### Version resolution

- **[lotus-version-management.yml](./lotus-version-management.yml)** —
  reusable workflow that returns the version string. On a tag push it
  returns the tag value; on any other run it returns
  `<cmakelists-version>-dev-<short-sha>`. No auto-bump, no auto-revert.

### Build matrix workflows (reusable)

| Workflow | Targets | Runs on |
| --- | --- | --- |
| **[lotus-build-linux.yml](./lotus-build-linux.yml)** | `lotusd`, `lotus-cli`, `lotus-tx`, `lotus-seeder`, `lotus-wallet`, `lotus-qt` × `linux-amd64` via `depends/` | Every run (release **and** dev) |
| **[lotus-build-windows.yml](./lotus-build-windows.yml)** | All components × `windows-amd64` (mingw32) | Release only |
| **[lotus-build-macos.yml](./lotus-build-macos.yml)** | All components × `macos-{x86_64, arm64}` via `depends/` darwin (gated by `MACOS_SDK_URL`) | Release only |
| **[lotus-build-docker.yml](./lotus-build-docker.yml)** | `linux/amd64` Docker manifests for every component pushed to `ghcr.io/lotusdevorg/*` | Release only |
| **[lotus-build-gpu-miner.yml](./lotus-build-gpu-miner.yml)** | `lotus-gpu-miner` (NVIDIA: amd64) and `lotus-gpu-miner-amd` (ROCm: amd64), both as binaries and as Docker manifests | Release only |

> **Architecture coverage caveat (11.0.x):** the initial 11.x release is
> **amd64 only** across Linux + Docker + GPU miner. arm64 / armv7 are
> blocked by an upstream Boost 1.70 cross-compile bug
> (`boost/thread/pthread/thread_data.hpp` PAGE_SIZE preprocessor); the
> Jetson NVIDIA miner is additionally blocked on an `openssl-sys`
> cross-sysroot. Both are tracked for a follow-up release that will
> patch the relevant `depends/packages/*.mk`.

### Release

- **[lotus-release.yml](./lotus-release.yml)** — collects artifacts from
  every release-grade build, builds combined per-`(os,arch)` packages,
  and publishes a GitHub release attached to the triggering tag with
  auto-generated notes (logo, binaries section, Docker section, changelog
  vs previous tag, install instructions, system requirements, links).

## Dependency graph

```
lotus-main-ci.yml
├── classify                       (release vs dev)
├── lotus-version-management.yml
├── lotus-build-linux.yml          (always)
├── lotus-build-windows.yml        (release only)
├── lotus-build-macos.yml          (release only, requires MACOS_SDK_URL)
├── lotus-build-docker.yml         (release only)
├── lotus-build-gpu-miner.yml      (release only)
└── lotus-release.yml              (release only, after all build legs succeed)
```

## How to cut a release

```bash
# 1. Bump the version in source.
sed -i 's/VERSION 11.0.0/VERSION 11.0.1/' CMakeLists.txt
sed -i 's/pkgver=11.0.0/pkgver=11.0.1/'   contrib/aur/lotus/PKGBUILD
git add CMakeLists.txt contrib/aur/lotus/PKGBUILD
git commit -m "Bump version to 11.0.1"
git push origin master

# 2. Tag the bump commit and push the tag - this triggers the full pipeline.
git tag 11.0.1
git push origin 11.0.1
```

Within ~1-2 hours the GitHub Actions run will:
1. Build every component for every (os, arch) pair.
2. Push multi-arch Docker manifests to `ghcr.io/lotusdevorg/lotus-*:11.0.1`,
   `:sha-…`, and `:latest`.
3. Publish a GitHub release on the `11.0.1` tag with all binaries
   attached and auto-generated notes.

## Required repository configuration

| Setting | Purpose |
| --- | --- |
| `secrets.GITHUB_TOKEN` | Auto-injected; used to publish releases and push to `ghcr.io`. |
| `secrets.MACOS_SDK_URL` (optional) | Pre-signed download URL for a MacOSX SDK tarball used by the macOS cross-build job. The macOS leg is skipped (with a warning) if absent. |
| Repository setting → Actions → "Workflow permissions" | Must allow read/write so the release can be published. |
| Repository setting → Packages | Confirm the `ghcr.io/<owner>/lotus-*` packages are visible to the org. |

## Caching strategy

- **`depends/built` per host triple** cached with `actions/cache` keyed
  on the hash of `depends/packages/*.mk`, `depends/funcs.mk`, and
  `depends/Makefile`. Saves 30-40 min per matrix cell once warm.
- **`ccache`** persisted with `actions/cache` per OS/arch/component.
- **`buildx` layer cache** persisted via `type=gha` per Dockerfile
  (depends images, component images, GPU miner images).

## Build artefact naming

```
lotus-<component>-<version>-<os>-<arch>.{tar.gz,zip}
lotus-binaries-<version>-<os>-<arch>.{tar.gz,zip}     # all components bundled
```

Where `<component>` ∈
`{node, cli, tx, seeder, wallet, qt, gpu-miner-nvidia, gpu-miner-amd}`,
`<os>` ∈ `{linux, windows, macos}`, and `<arch>` is the `(os, arch)`
combination produced by the matrix. On dev builds `<version>` carries the
`-dev-<sha>` suffix to make it obvious the binary is not from a release
tag.

## Contributing

- Test workflow changes by pushing to a branch (every push triggers the
  Linux dev build) or via `workflow_dispatch`.
- Keep workflows focused on a single concern; reuse `workflow_call`
  instead of duplicating steps.
- Update this README whenever a new workflow is introduced or the matrix
  shape changes.
