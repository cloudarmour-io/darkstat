# Packaging and Config Files

This branch uses a Debian-style wrapper plus nfpm packaging metadata.

NOTE: the nfpm package (`packaging/nfpm.yaml`) is the supported build. The
Debian-style files in `debian/` (`control`, `postinst`, `rules`, and the old
`darkstat` package layout) are legacy and are not used by it. Only
`darkstat-nw-wrapper`, `darkstat-nw.default` and `darkstat-nw.service` from
`debian/` are shipped.

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
- `BINDADDR="127.0.0.1"`
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
- `BINDADDR` selects the address(es) darkstat binds to (default loopback only). Loopback and private addresses need no auth; any other bind (including `0.0.0.0`/`::`) requires `API_KEY`, otherwise those listeners return 401.
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


## Install and Start Behavior

- The `postinst` script creates the `darkstat-nw` user and `/var/lib/darkstat-nw`,
  and runs `systemctl enable darkstat-nw`. It deliberately does **not** start the
  service. Reason: the operator should review `/etc/default/darkstat-nw`
  (interfaces, bind address, `API_KEY`) before a packet-capturing daemon runs.
- Start it with `sudo systemctl start darkstat-nw` (then `systemctl status darkstat-nw`).
  It starts automatically on the next boot because it is enabled.
- `INTERFACES` is auto-detected at install time from the default route
  (`ip route show default`) and written into `/etc/default/darkstat-nw` when it
  is blank. If no default route exists then, it is left blank and the installer
  tells you to edit it.
- At service start, if `INTERFACES` is still blank the wrapper tries the default
  route again. With no default route it logs an error and exits 1; the unit has
  `Restart=always` / `RestartSec=5s`, so systemd retries every 5 seconds until
  `INTERFACES` is set or a default route appears.
- The unit runs as root only to open capture sockets; darkstat drops privileges
  itself (`--user darkstat-nw`).

## NeuroWall Integration

NeuroWall's `/api/v1/darkstat` returns HTTP 409 when darkstat is disabled in
NeuroWall's own configuration. It does not return 409 because darkstat is down
or unreachable.
