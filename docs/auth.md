# API Key and Bind-Aware Authentication

Darkstat now supports bind-aware authentication.

## Behavior

- A listener on a **private** address needs no authentication: loopback (`127.0.0.0/8`, `::1`), RFC 1918 (`10.0.0.0/8`,
  `172.16.0.0/12`, `192.168.0.0/16`), link-local (`169.254.0.0/16`, `fe80::/10`) and IPv6 unique-local (`fc00::/7`).
  This is `sockaddr_is_private_bindaddr` in `http.c`; an earlier version of this page wrongly said "only loopback".
- A listener on any other address, including the wildcards `0.0.0.0` and `::`, requires the API key.
- With such a listener and no `API_KEY` configured, darkstat still starts, warns, and answers every request on that
  listener with `401 Unauthorized`.
- Because private LAN addresses are exempt, binding to a LAN address exposes the traffic data to everyone on that LAN
  without a password. The packaged default therefore binds `127.0.0.1` only; add a LAN address deliberately.

## How It Works

The auth flow is:

1. The user types the plain API password.
2. Darkstat computes the MD5 hash of that password.
3. Darkstat compares the hash to the configured `API_KEY`.
4. If they match, the request is allowed.
5. If they do not match, darkstat returns `401 Unauthorized`.

The important distinction is:

- `API_KEY` in the config is stored as an MD5 hash
- browsers and backend clients should send the plain password
- darkstat hashes the entered password and compares the hashes

## Config Value

`API_KEY` lives in `debian/darkstat-nw.default` and is passed to the daemon as
`--api-key-md5`.

The value should be the MD5 hash of the password, not the plaintext password.

To generate it:

```sh
printf '%s' '1234' | md5sum
```

Example output:

```text
81dc9bdb52d04dc20036dbd8313ed055  -
```

Example:

```sh
API_KEY=81dc9bdb52d04dc20036dbd8313ed055
```

That example corresponds to the password `1234`.

## Notes

- The browser uses HTTP Basic auth style prompting, so it shows both username
  and password fields.
- The username field is ignored by darkstat; only the password is checked.
- The hash is stored in config, but the password is still the user-facing secret.
- This is intentionally simple and fits the existing embedded HTTP server.
