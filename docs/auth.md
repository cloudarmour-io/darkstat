# API Key and Bind-Aware Authentication

Darkstat now supports bind-aware authentication.

## Behavior

- If darkstat binds only to `127.0.0.1` or `::1`, no authentication is needed.
- If darkstat binds to any other address, authentication is required.

## How It Works

The auth flow is:

1. The user types the plain API password.
2. Darkstat computes the MD5 hash of that password.
3. Darkstat compares the hash to the configured `API_KEY`.
4. If they match, the request is allowed.
5. If they do not match, darkstat returns `401 Unauthorized`.

## Config Value

`API_KEY` lives in `debian/darkstat-nw.default` and is passed to the daemon as
`--api-key-md5`.

The value should be the MD5 hash of the password, not the plaintext password.

Example:

```sh
API_KEY=81dc9bdb52d04dc20036dbd8313ed055
```

That example corresponds to the password `1234`.

## Notes

- The browser uses HTTP Basic auth style prompting.
- The hash is stored in config, but the password is still the user-facing secret.
- This is intentionally simple and fits the existing embedded HTTP server.

