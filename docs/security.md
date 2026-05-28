# Security Model

NetScan enforces access control via API keys at the HTTP layer. Origin (local process, SSH tunnel, remote) is not considered — only the auth header matters.

Default binding is `127.0.0.1`. With no stored admin key this is **loopback open mode**: any local process has full API access.
Setup mode is also loopback-only. The browser-based setup server binds only to `127.0.0.1`; remote setup requires SSH tunneling or equivalent local port forwarding.

## Binding Policy

Non-loopback binding requires both an admin API key and TLS — startup fails if either is missing.

| Configuration | Result |
|---|---|
| `host=127.0.0.1` + no admin key | Loopback open mode — allowed |
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

Two-tier global token buckets, selected by auth status — no per-IP tracking. Designed for loopback deployments.

| Tier | Burst | Sustained |
|---|---|---|
| Authenticated (valid admin/restricted key, or open mode) | 20 | 5 req/s |
| Unauthenticated | 5 | 1 req/s |

A sliding-window lockout activates after 5 failed auth attempts within 60 seconds: unauthenticated requests return `429` for 5 minutes. Authenticated requests are not affected by the lockout.

**Reverse-proxy deployments:** If NetScan is placed behind nginx, Caddy, or similar, apply additional rate limiting at the proxy layer. NetScan's built-in limiter is intentionally minimal and not a substitute for a proxy-level WAF or rate-limit module.
