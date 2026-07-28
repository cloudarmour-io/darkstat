# Packaging and Config Files

This branch uses a Debian-style wrapper plus nfpm packaging metadata.

## Main Files

- `debian/darkstat-nw-wrapper`
- `debian/darkstat-nw.default`
- `debian/darkstat-nw.service`
- `packaging/nfpm.yaml`
- `packaging/scripts/*`

## `debian/darkstat-nw.default`

This file is the runtime configuration source for the packaged service.
It is read by the wrapper, not by darkstat directly.

Typical values in this branch:

- `START_DARKSTAT=true`
- `INTERFACES=eth0`
- `PORT=1667`
- `BINDADDR=`
- `BASE_PATH=/`
- `HOSTS_MAX=1000`
- `HOSTS_KEEP=500`
- `PORTS_MAX=60`
- `PORTS_KEEP=30`
- `HIGHEST_PORT=65535`
- `HOST_RETENTION_HOURS=1`
- `MEM_LIMIT_MB=256`
- `API_KEY=`

## Meaning of the Config

- `START_DARKSTAT` controls whether the wrapper starts the service.
- `INTERFACES` is the list of interfaces to monitor.
- `PORT` is the HTTP port for the web UI.
- `BINDADDR` selects the address darkstat binds to.
- `BASE_PATH` sets the UI base path.
- `HOSTS_MAX` and `HOSTS_KEEP` control host-table trimming.
- `PORTS_MAX` and `PORTS_KEEP` control per-host port trimming.
- `HIGHEST_PORT` limits the highest port number tracked.
- `HOST_RETENTION_HOURS` removes inactive hosts after the given time.
- `MEM_LIMIT_MB` sets an approximate memory cap.
- `API_KEY` stores the MD5 hash of the password used for auth.

## Wrapper Behavior

The wrapper:

1. loads defaults from `/etc/default/darkstat-nw`
2. auto-detects an interface if `INTERFACES` is blank
3. builds the final darkstat command line
4. forwards each config value to the matching CLI flag

This keeps the package configuration separate from the binary.

