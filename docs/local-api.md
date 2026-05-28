# Local API v1

HTTP API for the bundled web UI and local/tunneled clients.

## Base URL

- TLS off (default): `http://<host>:<port>`
- TLS on: `https://<host>:<port>`

See [tls-setup.md](tls-setup.md) for TLS configuration.

## Authentication

Access is determined solely by the request header — origin (local, SSH-tunneled, remote) is not considered.

| Header | Access |
|--------|--------|
| none | Full access in loopback open mode only |
| `X-API-Key` | Full admin access |
| `X-NetScan-Restricted-Key` | Restricted access (route + target allowlist) |

- Open mode requires loopback binding and no admin key hash present
- Both headers on the same request → `400 Bad Request`
- Keys are header-only; query params and body keys are not supported

**Restricted key allowlist:**

```
GET  /api/health        GET  /api/dashboard      GET  /api/settings
GET  /api/hosts         GET  /api/hosts/:ip       GET  /api/scans
GET  /api/scans/:id     GET  /api/scans/:id/diff  GET  /api/scans/:id/topology
POST /api/scan/start    GET  /api/scan/status     POST /api/scan/:id/abort
```

Restricted key scan rules:
- Target must be IPv4 literal or CIDR within `user_allowed_targets`; hostnames rejected
- `user_allowed_targets` is IPv4-only
- Targets outside the allowlist → `403 Forbidden`

**Key generation:**

```
./netscan --generate-key admin   # stores admin_api_key hash
./netscan --generate-key user    # stores restricted_api_key hash
```

Plaintext key printed once to stdout; never stored in `conf.ini`.

## Shared Shapes

### `ApiError`

```json
{ "status": "error", "type": "bad_request", "message": "invalid scan id" }
```

- `type`: stable machine-readable category
- `message`: human-readable explanation

### `ScanSummary`

```json
{
  "id": 12,
  "target": "127.0.0.1",
  "state": "completed",
  "message": "scan completed",
  "command": "nmap -Pn 127.0.0.1 -p 443",
  "stderrText": "",
  "exitCode": 0,
  "createdAt": "2026-01-01 00:00:00",
  "startedAt": "2026-01-01 00:00:01",
  "finishedAt": "2026-01-01 00:00:02",
  "deleted": false,
  "requestedPorts": "443",
  "scanType": "port_scan",
  "hostDiscoveryOnly": false,
  "portCoverageKnown": true
}
```

- `state`: `queued` | `running` | `aborted` | `completed` | `failed` | `dependency_missing`
- `exitCode`: integer or `null`
- `startedAt` / `finishedAt`: empty string before completion
- `requestedPorts`: string as sent, or `null` for default range
- `scanType`: `host_discovery` | `port_scan`
- `hostDiscoveryOnly`: `true` for `-sn` scans
- `portCoverageKnown`: `true` when port coverage is deterministic
- `deleted`: always `false` in list/detail responses (non-deleted only exposed)

### `ScanDiff`

```json
{
  "scan": {},
  "baseline": {},
  "hasBaseline": true,
  "comparable": true,
  "newHosts": [{ "ip": "192.168.1.30", "name": "camera.local", "acknowledgementKey": "new_host|192.168.1.30|0", "acknowledged": false }],
  "disappearedHosts": [],
  "newOpenPorts": [{ "ip": "192.168.1.20", "port": 443, "name": "printer.local", "service": "https", "acknowledgementKey": "new_open_port|192.168.1.20|443", "acknowledged": true }],
  "disappearedPorts": []
}
```

- `scan` / `baseline`: full `ScanSummary` shapes; `baseline` is `null` when none exists
- `reason`: `"no_baseline"` when `hasBaseline` is false
- `acknowledgementKey`: stable identity string per category/IP/port; format: `<category>|<ip>|<port>`
- `acknowledged`: presentation flag only; scan snapshots are immutable

### `TopologyGraph`

Returned by `GET /api/scans/:id/topology`.

```json
{
  "nodes": [
    { "id": "gw", "type": "gateway", "label": "NetScan", "meta": { "host_discovery_only": "false", "scan_target": "192.168.1.0/24" } },
    { "id": "host::192.168.1.20", "type": "host", "label": "printer.local", "meta": { "ip": "192.168.1.20", "name": "printer.local" } },
    { "id": "port::192.168.1.20::443/tcp", "type": "port", "label": "443/tcp", "meta": { "ip": "192.168.1.20", "port": "443", "protocol": "tcp", "service": "https", "state": "open" } }
  ],
  "edges": [
    { "id": "gw--host::192.168.1.20", "source": "gw", "target": "host::192.168.1.20" },
    { "id": "host::192.168.1.20--port::192.168.1.20::443/tcp", "source": "host::192.168.1.20", "target": "port::192.168.1.20::443/tcp" }
  ]
}
```

Node `id` patterns: `gw`, `host::<ip>`, `port::<ip>::<port>/<proto>`

Edge rules: gateway→host uses `gw` as source; host→port uses `host::<ip>` as source.

Topology notes:
- Computed on demand; only open ports produce port nodes
- Pure `up` hosts with no open ports are omitted from port-scan topologies
- `meta` values are strings even for numeric/boolean fields

### `HostOverview`

```json
{
  "id": 4, "ip": "192.168.1.20", "name": "printer.local",
  "scanCount": 3, "lastSeenAt": "2026-01-01 00:00:02", "lastScanId": 12, "openPortCount": 2,
  "meta": { "displayName": "Printer", "role": "printer", "tags": "internal" }
}
```

`meta` is always present; unset fields are `""`.

### `HostDetail`

```json
{
  "host": { "id": 4, "ip": "192.168.1.20", "name": "printer.local" },
  "meta": { "displayName": "Printer", "role": "printer", "tags": "internal" },
  "history": [
    {
      "scan": { "id": 12, "state": "completed", "createdAt": "2026-01-01 00:00:00", "startedAt": "2026-01-01 00:00:01", "finishedAt": "2026-01-01 00:00:02", "deleted": false },
      "ports": [{ "port": 443, "service": "https", "state": "open" }]
    }
  ]
}
```

## Endpoints

### `GET /api/health`

- `200 OK` — `status`, `checks` (`database`, `nmap`), `message`
- `500` — `ApiError`

Missing nmap degrades health but does not always block the backend.

### `GET /api/dashboard`

- `200 OK` — aggregated scan/host/port statistics

### `GET /api/settings`

- `200 OK`

```json
{ "settings": { "log_level": "info", "scan_cooldown_seconds": 0, "user_allowed_targets": ["192.168.1.0/24"] }, "restartRequired": false }
```

`user_allowed_targets` is stored in the database, not `conf.ini`. Settings apply live; `restartRequired` is always `false`.

### `POST /api/settings`

```json
{ "settings": { "log_level": "info", "scan_cooldown_seconds": 0, "user_allowed_targets": ["192.168.1.0/24"] } }
```

- `log_level`: required — `debug` | `info` | `warn` | `error`
- `scan_cooldown_seconds`: optional integer `>= 0`
- `user_allowed_targets`: optional array of IPv4/CIDR strings; takes effect immediately

Success `200 OK`:
```json
{ "status": "ok", "message": "settings saved", "restartRequired": false, "warnings": ["..."] }
```
`warnings` omitted when empty.

The request body **must** use the `{"settings":{...}}` envelope; a flat body returns `400` with a message indicating the required wrapper.

Failures: `400` validation, `500` write failure.

### `POST /api/scan/start`

```json
{ "target": "192.168.1.0/24", "ports": "22,80,443", "host_discovery_only": false }
```

- `target`: required — IPv4, IPv6, CIDR, or `localhost`
- `ports`: optional string (`22`, `22,80`, `1-1024`); mutually exclusive with `port`
- `port`: optional integer `1–65535` (compatibility alias for `ports`)
- `host_discovery_only`: optional boolean (default `false`)

Success `202 Accepted`:
```json
{ "status": "queued", "scan": { "id": 12, "state": "queued" } }
```

Failures: `400` bad request, `403` restricted key target outside allowlist, `409` concurrent scan for same target, `429` cooldown active, `500` internal error, `503` nmap unavailable.

Note: `scripts/scan_start_fuzz.py` can exercise this endpoint with payload files.

### `GET /api/scan/status`

- `200 OK` — `{ "status": "idle", "scan": null }` or `{ "status": "<state>", "scan": ScanSummary }`
- When running, may also include `progress` (0–100), `etaSeconds`, `hostsFound` (omitted until available)

### `GET /api/scans`

Query params: `target` (substring), `state` (exact), `from` / `to` (`YYYY-MM-DD HH:MM:SS`), `limit`, `offset`

- `200 OK` — `{ "scans": [ScanSummary] }`

### `POST /api/scan/:id/abort`

- `200 OK` — `{ "status": "ok", "scanId": 12 }`
- `400` invalid id, `404` not found, `409` not running, `500` termination failed

### `GET /api/scans/:id`

`ScanSummary` extended with:

```json
{ "hosts": [{ "ip": "192.168.1.20", "hostname": "printer.local", "openPortCount": 2 }] }
```

`hosts` is empty until scan completes.

Failures: `400` invalid id, `404` not found or deleted.

### `GET /api/scans/:id/diff`

- `200 OK` — `ScanDiff`
- `400` invalid id, `404` not found, `409` not diffable (not completed)

Only completed non-deleted scans are diffable. A successful response may have `baseline: null` and `reason: "no_baseline"`.

### `PUT /api/scan-diff/acknowledgements`

```json
{ "category": "new_open_port", "ip": "192.168.1.20", "port": 443 }
```

- `category`: `new_host` | `disappeared_host` | `new_open_port` | `disappeared_port`
- `port`: required for port categories (`1–65535`); omit or `null` for host categories

Success `200 OK`:
```json
{ "status": "ok", "acknowledgement": { "category": "new_open_port", "ip": "192.168.1.20", "port": 443, "acknowledgementKey": "new_open_port|192.168.1.20|443", "acknowledged": true } }
```

Failures: `400` validation, `500` persistence failure.

### `DELETE /api/scan-diff/acknowledgements`

Same request body as `PUT`. Returns same shape with `"acknowledged": false`.

Failures: `400`, `500`.

### `GET /api/scans/:id/topology`

- `200 OK` — `TopologyGraph`
- `400` invalid id, `404` not found, `409` not completed

### `DELETE /api/scans/:id`

Soft-deletes a completed scan.

- `200 OK` — `{ "status": "ok", "deletedScanId": 12 }`
- `400` invalid id, `404` not found, `409` not deletable, `500` delete failure

### `GET /api/hosts`

Query params: `q` (hostname/IP substring), `openPortsOnly` (`true`), `limit`, `offset`

- `200 OK` — `{ "hosts": [HostOverview], "total": 42, "page": 1, "pageSize": 20 }`

### `PATCH /api/hosts/:ip/meta`

All fields optional; only provided fields are updated.

```json
{ "displayName": "My Printer", "role": "printer", "tags": "internal,managed" }
```

- `200 OK` — updated meta object
- `400` validation, `404` host not found

### `DELETE /api/hosts/:ip/meta/:field`

`field`: `displayName` | `role` | `tags`

- `200 OK`
- `400` invalid field, `404` host not found

### `GET /api/hosts/:ip`

- `200 OK` — `HostDetail`
- `400` invalid parameter, `404` host not found

### `DELETE /api/hosts/ports/closed`

Removes all closed-state port records across all hosts.

- `200 OK` — `{ "status": "ok", "deleted": 42 }`
- `500` prune failure

## Scheduled Scanning

### `GET /api/scheduler/jobs`

- `200 OK` — `{ "jobs": [{ "id", "target", "ports", "hostDiscoveryOnly", "intervalSeconds", "enabled", "lastRunAt", "nextRunAt" }] }`

### `POST /api/scheduler/jobs`

```json
{ "target": "192.168.1.0/24", "ports": "22,80,443", "host_discovery_only": false, "interval_seconds": 3600, "enabled": true }
```

- `target`, `interval_seconds`: required
- `ports`, `host_discovery_only` (default `false`), `enabled` (default `true`): optional

Success `201 Created` — `{ "job": { ...full job object... } }`

Failures: `400` validation, `500` save failure.

### `DELETE /api/scheduler/jobs/:id`

- `204 No Content`
- `400` invalid id, `404` not found

### `PATCH /api/scheduler/jobs/:id`

```json
{ "enabled": true }
```

- `200 OK` — `{ "job": { "id": 1, "enabled": true } }`
- `400` validation, `404` not found

## Scan Notes

### `GET /api/scans/:id/notes`

- `200 OK` — `{ "notes": [{ "id", "scanId", "body", "createdAt" }] }`
- `400` invalid id, `404` scan not found

### `POST /api/scans/:id/notes`

```json
{ "body": "Investigation notes here" }
```

Success `201 Created` — `{ "note": { "id", "scanId", "body", "createdAt" } }`

Failures: `400`, `404` scan not found, `500`.

### `DELETE /api/scans/:id/notes/:noteId`

- `200 OK` — `{ "status": "ok", "deletedNoteId": 1 }`
- `400` invalid ids, `404` scan or note not found, `500`.

## Scan Profiles

### `GET /api/profiles`

- `200 OK` — `{ "profiles": [{ "id", "name", "target", "ports", "hostDiscoveryOnly", "createdAt" }] }`

### `POST /api/profiles`

```json
{ "name": "Internal Network Scan", "target": "192.168.1.0/24", "ports": "22,80,443", "host_discovery_only": false }
```

- `name`, `target`: required
- `ports`, `host_discovery_only` (default `false`): optional

Success `201 Created` — `{ "profile": { ...full profile object... } }`

Failures: `400`, `500`.

### `PUT /api/profiles/:id`

Same request body as `POST /api/profiles`. Replaces all fields.

- `200 OK` — `{ "profile": { ...updated profile... } }`
- `400`, `404`, `500`

### `DELETE /api/profiles/:id`

- `204 No Content`
- `400` invalid id, `404` not found

### `POST /api/profiles/:id/run`

Queues an immediate scan using the profile's saved settings.

- `200 OK` — `{ "status": "queued", "scan": { "id": 12, "state": "queued" } }`
- `400` invalid id, `404` profile not found, `409` concurrent scan for same target, `500`, `503` nmap unavailable

## Presence Tracking

### `GET /api/presence/trackers`

- `200 OK` — `{ "trackers": [{ "id", "target", "intervalSeconds", "enabled", "lastCheckedAt", "lastStatus" }] }`

### `POST /api/presence/trackers`

```json
{ "target": "192.168.1.1", "interval_seconds": 60, "enabled": true }
```

- `target`, `interval_seconds`: required
- `enabled` (default `true`): optional

Success `201 Created` — `{ "tracker": { ...full tracker object... } }`

Failures: `400` validation, `500` save failure.

### `GET /api/presence/trackers/:id`

- `200 OK` — `{ "tracker": { ...full tracker object... } }`
- `400` invalid id, `404` not found

### `PATCH /api/presence/trackers/:id`

Same request body as `POST /api/presence/trackers`. Replaces all fields.

- `200 OK` — `{ "tracker": { ...updated tracker... } }`
- `400`, `404`, `500`

### `DELETE /api/presence/trackers/:id`

- `204 No Content`
- `400` invalid id, `404` not found

### `POST /api/presence/trackers/:id/check`

Triggers an immediate presence check for the tracker.

- `200 OK` — `{ "result": { ...presence result... } }`
- `400`, `404`, `500`

### `GET /api/presence/trackers/:id/results`

- `200 OK` — `{ "results": [{ "id", "trackerId", "status", "checkedAt" }] }`
- `400` invalid id, `404` not found

## Administrative Endpoints

### `POST /api/settings/setup`

Restarts into setup mode. Optional body may override only the setup port.

```json
{ "port": 8080 }
```

- `200 OK` — `{ "status": "ok", "setup_url": "http://127.0.0.1:8080" }`
- The setup server always binds to `127.0.0.1`. Remote access requires SSH tunneling or equivalent local port forwarding.
- Server restarts immediately after response. Not available with `X-NetScan-Restricted-Key`.

### `POST /api/setup/reset`

Deletes `conf.ini` and restarts into fresh setup mode. `netscan.db` is preserved.

- `200 OK` — `{ "status": "ok" }`
- Server restarts immediately after response. Not available with `X-NetScan-Restricted-Key`.

### `POST /api/shutdown`

Graceful server shutdown. Terminates any running scan before exit.

- `200 OK` — `{ "status": "shutdown_requested" }`

## Host Persistence Guarantee

All hosts with `status="up"` in nmap output are persisted regardless of open port count:

- **Host discovery** (`-sn`): all up hosts stored, no port data
- **Port scan** (`-Pn`): all up hosts stored, including those with zero open ports

This ensures diffs correctly distinguish host availability changes from port changes.
