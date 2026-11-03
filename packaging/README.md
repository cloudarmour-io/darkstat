# darkstat-nw Packaging Guide

NOTE: the nfpm package (`packaging/nfpm.yaml`) is the supported build. The
Debian-style files in `debian/` (`control`, `postinst`, `rules`, and the old
`darkstat` package layout) are legacy and are not used by it. Only
`darkstat-nw-wrapper`, `darkstat-nw.default` and `darkstat-nw.service` from
`debian/` are shipped.

This directory contains the nfpm-based packaging setup for building `.deb` and `.rpm` packages for darkstat-nw across `amd64` and `arm64` architectures.

## Prerequisites

- [nfpm](https://nfpm.goreleaser.com/install/) — `go install github.com/goreleaser/nfpm/v2/cmd/nfpm@latest`
- Build tools: `autoconf`, `automake`, `libpcap-dev`, `zlib1g-dev`
- For arm64 cross-compilation: `gcc-aarch64-linux-gnu`, `libpcap-dev:arm64`, `zlib1g-dev:arm64`

## How It Works

```
source code
    │
    ▼
make (produces ./darkstat binary)
    │
    ▼
nfpm package (reads packaging/nfpm.yaml)
    │
    ├── .deb  →  packaging/dist/darkstat-nw_<version>_<arch>.deb
    └── .rpm  →  packaging/dist/darkstat-nw-<version>.<arch>.rpm
```

`nfpm.yaml` defines what goes into the package — binary, wrapper script, systemd unit, default config, man page, and the pre/post install scripts under `packaging/scripts/`.

The `$GOARCH` environment variable controls the target architecture label embedded in the package metadata. It does not cross-compile — you must build the binary for the target arch separately.

## One-Command Packaging

From the repository root, you can build both `amd64` and `arm64` packages with a single command:

```bash
./package -v 1.1.0
```

This will:

1. Build the native `amd64` binary and package it as `.deb` and `.rpm`
2. Cross-compile the `arm64` binary and package it as `.deb` and `.rpm`
3. Write all outputs to `packaging/dist/`

Use `-v` or `--version` to set the package version used by `nfpm`.

## Build Locally (amd64)

```bash
# 1. Build the binary
autoreconf -fi
./configure --prefix=/usr --sbindir=/usr/sbin --with-privdrop-user=darkstat
make -j$(nproc)

# 2. Verify the binary really is amd64 (nfpm only labels the package; a stale cross-built binary would be packaged
#    into an "amd64" deb and fail on the target with "Exec format error")
packaging/check-binary-arch.sh darkstat amd64

# 3. Create output directory
mkdir -p packaging/dist

# 4. Build packages
VERSION=1.1.0 GOARCH=amd64 nfpm package --config packaging/nfpm.yaml --packager deb --target packaging/dist/
VERSION=1.1.0 GOARCH=amd64 nfpm package --config packaging/nfpm.yaml --packager rpm --target packaging/dist/
```

## Build Locally (arm64 cross-compile)

```bash
# Install cross toolchain and arm64 libs
sudo dpkg --add-architecture arm64
sudo apt-get update
sudo apt-get install -y gcc-aarch64-linux-gnu libpcap-dev:arm64 zlib1g-dev:arm64

# Build the binary for arm64
make clean
./configure --host=aarch64-linux-gnu --prefix=/usr --sbindir=/usr/sbin \
    --with-privdrop-user=darkstat CC=aarch64-linux-gnu-gcc
make -j$(nproc)

# Verify the binary really is arm64, then build packages
packaging/check-binary-arch.sh darkstat arm64
mkdir -p packaging/dist
VERSION=1.1.0 GOARCH=arm64 nfpm package --config packaging/nfpm.yaml --packager deb --target packaging/dist/
VERSION=1.1.0 GOARCH=arm64 nfpm package --config packaging/nfpm.yaml --packager rpm --target packaging/dist/
```

## Install & Test

```bash
# Debian/Ubuntu
sudo dpkg -i packaging/dist/darkstat-nw_3.0.722-1_amd64.deb
sudo systemctl status darkstat-nw

# RHEL/Fedora
sudo rpm -i packaging/dist/darkstat-nw-3.0.722-1.x86_64.rpm
sudo systemctl status darkstat-nw
```

## Package Contents

| Path | Description |
|---|---|
| `/usr/sbin/darkstat` | Main binary |
| `/usr/lib/darkstat-nw/darkstat-nw-wrapper` | Startup wrapper (reads `/etc/default/darkstat-nw`) |
| `/lib/systemd/system/darkstat-nw.service` | systemd unit |
| `/etc/default/darkstat-nw` | Runtime config (marked `config|noreplace`) |
| `/usr/share/man/man8/darkstat.8` | Man page |
| `/var/lib/darkstat-nw/` | State directory, owned by `darkstat-nw` user |

## Configuration

Edit `/etc/default/darkstat-nw` after install:

```sh
START_DARKSTAT=true
INTERFACES=eth0     # leave blank to auto-detect from default route
PORT=1667
BINDADDR=127.0.0.1     # loopback only; non-private binds require API_KEY
BASE_PATH=/
HOSTS_MAX=1000
HOSTS_KEEP=500
PORTS_MAX=60
PORTS_KEEP=30
HIGHEST_PORT=65535
HOST_RETENTION_HOURS=168
MEM_LIMIT_MB=0
API_KEY=
EXTRA_OPTS=
```

The package enables the service but does not start it; see `docs/packaging.md`.

If `INTERFACES` is left blank, the wrapper auto-detects it from the system default route via `ip route show default`. The detected interface is logged to the journal.

These variables map to the existing darkstat options:

- `HOSTS_MAX` -> `--hosts-max`
- `HOSTS_KEEP` -> `--hosts-keep`
- `PORTS_MAX` -> `--ports-max`
- `PORTS_KEEP` -> `--ports-keep`
- `HIGHEST_PORT` -> `--highest-port`
- `HOST_RETENTION_HOURS` -> `--host-retention-hours`
- `MEM_LIMIT_MB` -> `--mem-limit-mb`
- `API_KEY` -> `--api-key-md5`

Auth behavior:

- If every bind address is loopback or private (10/8, 172.16/12, 192.168/16, 169.254/16, `::1`, fc00::/7, fe80::/10), no auth is required.
- If darkstat binds to any other address (public, `0.0.0.0`, `::`), the web UI requires an API key; without `API_KEY` those listeners return 401.
- `API_KEY` must contain the MD5 hash of the password.
- The browser/client/backend sends the plain password, darkstat hashes it with MD5,
  and compares the result to `API_KEY`.

## CI / Automated Builds

The GitHub Actions workflow at `.github/workflows/package.yml` runs the full matrix automatically on push:

1. **Build** — compiles the binary for `amd64` and `arm64` in parallel
2. **Package** — runs nfpm for each arch × format combination (4 artifacts total)
3. **Release** — attaches all packages to a GitHub Release when a `v*` tag is pushed

To trigger a release:
```bash
git tag v3.0.722
git push origin v3.0.722
```

## Updating the Version

Pass `VERSION=x.y.z` at build time:

```bash
VERSION=1.2.0 GOARCH=amd64 nfpm package --config packaging/nfpm.yaml --packager deb --target packaging/dist/
```

In CI, the release workflow sets `VERSION` from the git tag automatically (e.g. tag `v1.2.0` → `VERSION=1.2.0`).
