# NetScan
<img width="1124" height="508" alt="image" src="https://github.com/user-attachments/assets/3857e685-0286-49c7-9a44-9219bf166c8f" />

Self-hosted HTTP network scanning and monitoring service written in C++14. NetScan combines an nmap-backed scanner, SQLite persistence, a REST API, and a browser UI for local network visibility.

## Features

- Network scanning via nmap: host discovery and port scans
- Scheduled recurring scans
- Scan diffing against previous results
- Scan profiles for reusable target/port presets
- Scan notes for investigation context
- Host presence tracking
- React web UI plus REST API

## Architecture

Five-layer, top-down dependency model:

| Layer | Path | Responsibility |
|---|---|---|
| HTTP | `src/http/` | Routing, auth guard, static files, API handlers |
| Service | `src/service/` | Business logic, scan lifecycle, scheduling, presence |
| DB | `src/db/` | SQLite repositories, RAII wrapper, schema |
| Scan | `src/scan/` | nmap command building, process management, XML parsing |
| Util | `src/util/` | Logger, paths, permissions, shared helpers |

## Building

Requirements: C++14 compiler, `make`, Node.js + npm. SQLite and the C++ header dependencies are bundled in `third_party/`.

```bash
make                  # full build: frontend + backend
make release          # platform-aware release target
make release-linux    # Linux AppImage release
make release-windows  # Windows release staging, MinGW/MSYS2 only
```

Frontend only:

```bash
cd resources/web
npm install
npm run build
```

Schema note: schema creation is idempotent, but there is no migration layer yet. During development, delete `netscan.db` after incompatible schema changes.

## Running

```bash
./netscan                    # Linux
./netscan.exe                # Windows server binary
./netscan -c /path/to/conf.ini
```

Default URL: `http://127.0.0.1:8080`.

## Runtime Layout

Development:

```text
./netscan
./conf.ini
./netscan.db
./resources/web/
```

Windows release staging:

```text
release/
├── NetScan.exe          # launcher
├── netscan-server.exe   # background server
├── stop.bat
├── conf.ini
└── resources/web/
```

Windows installed layout:

```text
%LOCALAPPDATA%\Programs\NetScan\   # app binaries and web assets
%LOCALAPPDATA%\NetScan\            # conf.ini, netscan.db, netscan.pid
```

Runtime paths resolve relative to the executable or config file. Exception: a relative `-c <path>` resolves from the invocation directory.

## Example `conf.ini`

```ini
host=127.0.0.1
port=8080
db_path=./netscan.db
web_dir=./resources/web
log_level=info
# log_file=./netscan.log
ui_enabled=false
scan_cooldown_seconds=0
tls_enabled=false
# tls_cert_path=./certs/netscan.crt
# tls_key_path=./certs/netscan.key
# nmap_path=/usr/bin/nmap
```

## Windows Installer

The Windows installer is built from the repo-local Inno Setup script after staging a Windows release.

Local installer build from the repository root:

```bash
make release-windows
/c/Users/<you>/AppData/Local/Programs/Inno\ Setup\ 6/ISCC.exe //DAppVersion=0.1.0 packaging/windows/netscan.iss
```

The GitHub Windows workflow runs `make test-all`, `make release-windows`, and then builds the same installer script.

The launcher is `NetScan.exe`; it starts `netscan-server.exe` in the background and opens the local UI.

## API

Full contract in [`docs/local-api.md`](docs/local-api.md).

| Group | Endpoints |
|---|---|
| Health | `GET /api/health`, `GET /api/dashboard` |
| Settings | `GET /api/settings`, `POST /api/settings` |
| Scans | `POST /api/scan/start`, `GET /api/scan/status`, `GET /api/scans`, `GET /api/scans/:id`, `POST /api/scan/:id/abort`, `DELETE /api/scans/:id` |
| Scan data | `GET /api/scans/:id/diff`, `GET /api/scans/:id/topology`, `PUT/DELETE /api/scan-diff/acknowledgements` |
| Hosts | `GET /api/hosts`, `GET /api/hosts/:ip`, `PATCH /api/hosts/:ip/meta`, `DELETE /api/hosts/:ip/meta/:field`, `DELETE /api/hosts/ports/closed` |
| Scheduler | `GET/POST /api/scheduler/jobs`, `PATCH/DELETE /api/scheduler/jobs/:id` |
| Notes | `GET/POST /api/scans/:id/notes`, `DELETE /api/scans/:id/notes/:noteId` |
| Profiles | `GET/POST /api/profiles`, `PUT/DELETE /api/profiles/:id`, `POST /api/profiles/:id/run` |
| Presence | `/api/presence` trackers and results |
| Admin | `POST /api/shutdown`, `POST /api/settings/setup`, `POST /api/setup/reset` |

## Security

NetScan binds to `127.0.0.1` by default. Non-loopback binding requires an admin API key and TLS. Setup mode also binds only to `127.0.0.1`; remote setup should use SSH tunneling or equivalent port forwarding. See [`docs/security.md`](docs/security.md) and [`docs/tls-setup.md`](docs/tls-setup.md).

## Scan Behavior

- Host discovery: `nmap -sn`
- Port scan: `nmap -Pn`, with `-p <ports>` when specified
- Concurrent scans for the same target return `409 Conflict`
- Abort from UI or API terminates the nmap child process
- All `status=up` hosts are persisted, even with zero open ports

## Third-party

Bundled in `third_party/`:

| Library | License |
|---|---|
| [SQLite 3](https://sqlite.org/) | Public domain |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT |
| [cxxopts](https://github.com/jarro2783/cxxopts) | MIT |
| [tinyxml2](https://github.com/leethomason/tinyxml2) | zlib |

Frontend: [React](https://react.dev/) and [esbuild](https://esbuild.github.io/), both MIT.

## License

MIT - see [LICENSE](LICENSE). Fork and modify freely; keep the copyright notice.
