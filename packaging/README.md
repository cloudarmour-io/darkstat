# darkstat Packaging Guide

This directory contains the nfpm-based packaging setup for building `.deb` and `.rpm` packages for darkstat across `amd64` and `arm64` architectures.

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
    ├── .deb  →  packaging/dist/darkstat_<version>_<arch>.deb
    └── .rpm  →  packaging/dist/darkstat-<version>.<arch>.rpm
```

`nfpm.yaml` defines what goes into the package — binary, wrapper script, systemd unit, default config, man page, and the pre/post install scripts under `packaging/scripts/`.

The `$GOARCH` environment variable controls the target architecture label embedded in the package metadata. It does not cross-compile — you must build the binary for the target arch separately.

## Build Locally (amd64)

```bash
# 1. Build the binary
autoreconf -fi
./configure --prefix=/usr --sbindir=/usr/sbin --with-privdrop-user=darkstat
make -j$(nproc)

# 2. Create output directory
mkdir -p packaging/dist

# 3. Build packages
GOARCH=amd64 nfpm package --config packaging/nfpm.yaml --packager deb --target packaging/dist/
GOARCH=amd64 nfpm package --config packaging/nfpm.yaml --packager rpm --target packaging/dist/
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

# Build packages
mkdir -p packaging/dist
GOARCH=arm64 nfpm package --config packaging/nfpm.yaml --packager deb --target packaging/dist/
GOARCH=arm64 nfpm package --config packaging/nfpm.yaml --packager rpm --target packaging/dist/
```

## Install & Test

```bash
# Debian/Ubuntu
sudo dpkg -i packaging/dist/darkstat_3.0.722-1_amd64.deb
sudo systemctl status darkstat

# RHEL/Fedora
sudo rpm -i packaging/dist/darkstat-3.0.722-1.x86_64.rpm
sudo systemctl status darkstat
```

## Package Contents

| Path | Description |
|---|---|
| `/usr/sbin/darkstat` | Main binary |
| `/usr/lib/darkstat/darkstat-wrapper` | Startup wrapper (reads `/etc/default/darkstat`) |
| `/lib/systemd/system/darkstat.service` | systemd unit |
| `/etc/default/darkstat` | Runtime config (marked `config|noreplace`) |
| `/usr/share/man/man8/darkstat.8` | Man page |
| `/var/lib/darkstat/` | State directory, owned by `darkstat` user |

## Configuration

Edit `/etc/default/darkstat` after install:

```sh
START_DARKSTAT=true
INTERFACE=eth0      # leave blank to auto-detect from default route
PORT=667
BINDADDR=
BASE_PATH=/
EXTRA_OPTS=
```

If `INTERFACE` is left blank, the wrapper auto-detects it from the system default route via `ip route show default`. The detected interface is logged to the journal.

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

Change `version:` in `packaging/nfpm.yaml`. The CI workflow picks it up automatically.
