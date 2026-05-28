# TLS Setup

NetScan supports HTTPS directly via a built-in TLS listener. **TLS does not replace API-key authentication** — both are required for a non-loopback deployment.

## Prerequisites

- NetScan built with `CPPHTTPLIB_OPENSSL_SUPPORT` (default for release builds)
- `openssl` CLI — only if generating certificates manually (see below)

## Generating a Certificate

### Option A: Built-in (recommended)

```bash
./netscan --generate-cert --ip 192.168.1.100
```

Generates `certs/netscan.crt` and `certs/netscan.key` next to the binary and automatically sets `tls_enabled=true`, `tls_cert_path`, and `tls_key_path` in `conf.ini`.

Options: `--days <n>` (default 365), `--force` (overwrite existing files).

### Option B: Manual openssl

```bash
mkdir -p certs
openssl req -x509 -newkey rsa:2048 \
  -keyout certs/netscan.key -out certs/netscan.crt \
  -days 365 -nodes -subj "/CN=netscan" \
  -addext "subjectAltName=IP:192.168.1.100"
```

Replace `192.168.1.100` with the IP NetScan will bind to. Browsers require the SAN for LAN access.

## conf.ini

```ini
host=192.168.1.100
port=8080
db_path=./netscan.db
web_dir=./resources/web
log_level=info
tls_enabled=true
tls_cert_path=./certs/netscan.crt
tls_key_path=./certs/netscan.key
```

Relative paths are resolved against the config file's directory.

## LAN Deployment Sequence

```bash
# 1. Generate certificate (auto-updates conf.ini)
./netscan --generate-cert --ip 192.168.1.100

# 2. Generate admin API key (required before non-loopback start)
./netscan --generate-key admin

# 3. Start
./netscan
```

Server logs `Server running on https://192.168.1.100:8080` when TLS is active.

## Startup Validation

Checks run in this order — the first failing check produces the error:

| Configuration | Result |
|---|---|
| `host=127.0.0.1` | Allowed (loopback HTTP or HTTPS) |
| `host=<LAN IP>` + no admin key | **Startup error** — generate key first |
| `host=<LAN IP>` + admin key + `tls_enabled=false` | **Startup error** — TLS required |
| `host=<LAN IP>` + admin key + `tls_enabled=true` + cert/key missing | **Startup error** — cert/key must exist and be readable |
| `host=<LAN IP>` + admin key + `tls_enabled=true` + cert/key present | Allowed |

## Testing

```bash
# Skip verification (testing only)
curl -sk -H "X-API-Key: <key>" https://192.168.1.100:8080/api/health

# Trust the cert explicitly
curl --cacert certs/netscan.crt -H "X-API-Key: <key>" https://192.168.1.100:8080/api/health
```

**Browsers:** Add a security exception (Chrome/Edge: Advanced → Proceed; Firefox: Advanced → Accept the Risk). For a trusted cert without exceptions, use a local CA or Let's Encrypt via a reverse proxy.
