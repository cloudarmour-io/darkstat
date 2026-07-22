# Darkstat Architecture

Darkstat is a network traffic monitor with an embedded HTTP server.
It captures packets, decodes them, and stores summaries in in-memory tables
that drive the web UI and exported statistics.

## Runtime Flow

1. `darkstat` starts and parses command-line options.
2. It initializes capture, DNS, caches, graph data, and host tables.
3. It opens the HTTP listener on the configured bind address and port.
4. It enters the main loop and alternates between:
   - polling capture input
   - handling HTTP requests
   - updating timers and retention logic
5. The web UI reads the in-memory tables to render pages.

## Data Flow

Traffic is processed in stages:

- capture layer reads packets from a live interface or a pcap file
- decode layer extracts protocol and address information
- accounting layer updates totals
- host database layer tracks hosts, ports, protocols, and last-seen time
- HTTP layer renders HTML, JSON, metrics, and graphs from those tables

## Memory Model

Darkstat keeps working state in RAM. The important tables are:

- hosts
- per-host ports
- per-host protocols
- graph/statistic history
- DNS and name caches

The host database now supports:

- count-based trimming using `HOSTS_MAX` and `HOSTS_KEEP`
- stale-host cleanup using `HOST_RETENTION_HOURS`
- approximate memory pressure trimming using `MEM_LIMIT_MB`

## Retention Rules

- `HOSTS_MAX` is the point where host trimming starts.
- `HOSTS_KEEP` is the target size after host trimming.
- `HOST_RETENTION_HOURS` removes hosts that have not been seen for that long.
- `MEM_LIMIT_MB` is an approximate memory cap for the host database.

The retention logic prefers older hosts first using `last_seen`.

