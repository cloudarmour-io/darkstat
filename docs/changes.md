# Branch Changes

This branch adds configuration, retention, packaging, testing, and auth-related
changes.

## Retention and Memory

- added host retention by age
- added approximate memory-based host eviction
- kept the existing count-based trimming behavior

## Packaging

- updated the Debian wrapper
- updated the default config
- updated packaging documentation
- added tests for config passthrough

## Testing

- added shell tests under `tests/`
- added coverage for defaults, wrapper wiring, and help output
- added a traffic replay test using a generated pcap

## Authentication

- added bind-aware auth behavior
- loopback-only binds skip auth
- non-loopback binds require auth
- added `API_KEY` config support using an MD5 hash

