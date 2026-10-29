# Security Model

NetScan enforces access control via API keys at the HTTP layer. Where a request comes from (local process, SSH tunnel, remote) does not grant access — only the auth header does. Browser requests are additionally checked for their origin (see below).

Default binding is `127.0.0.1`. With no stored admin key this is **loopback open mode**: any local process (CLI, script) has full API access. Web pages in a browser do not: cross-site writes and DNS-rebound requests are rejected by the browser request protection below.
Setup mode is also loopback-only. The browser-based setup server binds only to `127.0.0.1`; remote setup requires SSH tunneling or equivalent local port forwarding.

## Browser Request Protection (CSRF / DNS Rebinding)

Open mode trusts every local process, but not every web page the user has open in a browser.
A pre-routing guard on both the main server and the setup server enforces:

- **Host allowlist (loopback binds):** the `Host` header must name `localhost` or a loopback IP
  (`127.0.0.0/8`, `::1`); any port. Everything else gets `403 invalid_host`. This blocks
  DNS rebinding, including reads of the generated keys from `POST /api/setup` and `0.0.0.0`
  requests. Non-loopback binds skip this check: they always require an admin key and TLS, so a
  rebound page has neither the key nor a valid certificate for its own name.
  `allowed_hosts` in `conf.ini` adds further exact names (see *Reverse proxy* below); the setup
  server ignores it.
- **Same-origin writes:** `POST`/`PUT`/`PATCH`/`DELETE` on `/api/*` are rejected with
  `403 forbidden` when `Sec-Fetch-Site` is anything other than `same-origin`/`none`, or when an
  `Origin` header is present (including `null`) and does not match `Host`.
- **JSON only:** a write request with a body, or with any `Content-Type`, must use
  `Content-Type: application/json` (`415 unsupported_media_type`). Bodyless writes without a
  `Content-Type` (e.g. `curl -X DELETE`) remain allowed.

The setup server additionally sends `Cache-Control: no-store`, `X-Frame-Options: DENY` and
`frame-ancestors 'none'` so the key response is never cached and the wizard cannot be framed.

CLI clients that send no `Origin`/`Sec-Fetch-*` headers are unaffected.

### Reverse proxy in front of a loopback bind

A proxy on the same machine (nginx, Caddy) that forwards to `127.0.0.1` must:

1. forward the browser's `Host` header unchanged (nginx: `proxy_set_header Host $host;`,
   Caddy does this by default), so the `Origin` check compares the public name with itself, and
2. have that public name listed in `conf.ini`:

   ```ini
   allowed_hosts=netscan.example.org
   ```

`allowed_hosts` is a comma-separated list of exact hostnames or IP literals (no wildcards, no
ports; case-insensitive). Each entry is a name you vouch for: DNS rebinding through it is
possible only for whoever controls that name's DNS. A proxy that rewrites `Host` to
`127.0.0.1` makes every browser write fail with `403 forbidden`.

## Binding Policy

Non-loopback binding requires both an admin API key and TLS — startup fails if either is missing, and the setup wizards (CLI and browser) refuse such a configuration up front.

Loopback means an IP literal in `127.0.0.0/8` or `::1`. `host=localhost` is rewritten to `127.0.0.1` when the config is loaded or saved by setup, so the bind never depends on name resolution. Any other hostname counts as non-loopback.

| Configuration | Result |
|---|---|
| `host=127.0.0.1` + no admin key | Loopback open mode — allowed |
| `host=localhost` | Treated as `host=127.0.0.1` |
| `host=<LAN IP>` + no admin key | **Startup error** |
| `host=<LAN IP>` + admin key + `tls_enabled=false` | **Startup error** |
| `host=<LAN IP>` + admin key + `tls_enabled=true` + cert/key | Allowed |

## Restricted Key Target Allowlist

`user_allowed_targets` accepts only IPv4 literals and CIDR blocks. `POST /api/settings` rejects IPv6 entries with `400`. Admin-key scan requests are not restricted and can target IPv6.

## Remote Access Options

### Built-in TLS (recommended for direct LAN access)

```ini
host=192.168.1.100
port=8080
tls_enabled=true
tls_cert_path=./certs/netscan.crt
tls_key_path=./certs/netscan.key
```

```bash
./netscan --generate-key admin
./netscan --generate-key user   # optional restricted key
```

See [tls-setup.md](tls-setup.md) for certificate generation.

### SSH port forwarding

```bash
ssh -L 8080:127.0.0.1:8080 user@netscan-host
```

NetScan stays bound to loopback — no config change needed.

### nginx reverse proxy with TLS

```nginx
server {
    listen 443 ssl;
    server_name netscan.example.com;
    ssl_certificate     /etc/nginx/ssl/cert.pem;
    ssl_certificate_key /etc/nginx/ssl/key.pem;
    auth_basic           "NetScan";
    auth_basic_user_file /etc/nginx/.htpasswd;
    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

```bash
htpasswd -c /etc/nginx/.htpasswd netscan-user
```

### Caddy with automatic TLS

```caddyfile
netscan.example.com {
    basicauth {
        netscan-user $2a$14$HASHED_PASSWORD
    }
    reverse_proxy 127.0.0.1:8080
}
```

### VPN / WireGuard

Keep NetScan on loopback and access it through a VPN tunnel.

## Browser API Key Storage

The web UI stores the API key in `localStorage` under `ns_api_key` and sends it as `X-API-Key` on every request. Use **forget key** in Settings to clear it.

HTTP is safe only for `127.0.0.1` / `localhost`. Over plain HTTP on any other host the key is transmitted in cleartext. The Settings screen warns when a key is stored and the connection is non-loopback HTTP.

## Response Security Headers

Applied to all responses (API and static assets) via a post-routing handler.

| Header | Condition |
|--------|-----------|
| `X-Content-Type-Options: nosniff` | Always |
| `X-Frame-Options: DENY` | Always |
| `Cache-Control: no-store` | Always |
| `Content-Security-Policy: default-src 'self'; base-uri 'self'; frame-ancestors 'none'; object-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'self'` | Always |
| `Strict-Transport-Security: max-age=31536000` | `tls_enabled=true` only |

## Rate Limiting

Two-tier token buckets per client address (TCP peer address), selected by auth status. A client that exhausts its bucket or gets locked out does not affect other clients.

| Tier | Burst | Sustained |
|---|---|---|
| Authenticated (valid admin/restricted key, or open mode) | 40 | 10 req/s |
| Unauthenticated | 5 | 1 req/s |

A sliding-window lockout activates after 5 failed auth attempts within 60 seconds from the same address: its unauthenticated requests return `429` for 5 minutes. Authenticated requests are not affected by the lockout.

At most 1024 addresses are tracked; idle entries are dropped first, and if all are active, further addresses share one overflow bucket. On a loopback bind all local clients share `127.0.0.1`; behind a reverse proxy all clients share the proxy's address.

**Reverse-proxy deployments:** If NetScan is placed behind nginx, Caddy, or similar, apply additional rate limiting at the proxy layer. NetScan's built-in limiter is intentionally minimal and not a substitute for a proxy-level WAF or rate-limit module.
