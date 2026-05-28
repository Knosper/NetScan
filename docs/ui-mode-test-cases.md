# UI Mode Manual Test Cases

## TC-01: API-only mode without resources/web

`conf.ini`: `ui_enabled=false`, `resources/web` absent or empty.

```
./netscan --config conf.ini
```

- Starts without error
- `GET /` → `200 {"status":"ok","ui_enabled":false}`
- `GET /api/health` → `200`, `"ui_enabled": false`
- No web-asset errors in logs

## TC-02: UI mode with web assets present

`conf.ini`: `ui_enabled=true`, `resources/web/index.html` and `app.js` present (after `make frontend`).

```
./netscan --config conf.ini
```

- Starts without error
- `GET /` → `200` HTML (UI)
- `GET /api/health` → `200`, `"ui_enabled": true`

## TC-03: `--ui` flag overrides `ui_enabled=false`

`conf.ini`: `ui_enabled=false`, web assets present.

```
./netscan --config conf.ini --ui
```

- Starts with UI enabled
- `GET /` → `200` HTML
- `GET /api/health` → `"ui_enabled": true`

## TC-04: `--api-only` flag overrides `ui_enabled=true`

`conf.ini`: `ui_enabled=true`, `resources/web` may or may not exist.

```
./netscan --config conf.ini --api-only
```

- Starts without error, no web-asset check
- `GET /` → `200 {"status":"ok","ui_enabled":false}`
- `GET /api/health` → `"ui_enabled": false`
