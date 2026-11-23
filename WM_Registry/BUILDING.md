# Watering station Registry

WM_Registry is a separate Railway service. It enrolls stations over HTTPS and stores their credentials in its own SQLite database.

## Deploy on Railway

1. Create a service from this repository and set its **Root Directory** to `/WM_Registry`. Railway builds the Dockerfile in that directory. [Railway monorepo deployment](https://docs.railway.com/deployments/monorepo)
2. Attach a volume at `/var/lib/wm-registry`. That is the image's default `DB_DIR`, where Registry stores `stations.db`. [Railway volumes](https://docs.railway.com/volumes)
3. Set `PORT=8000` and use **8000** as the public domain's target port in **Networking → Public Networking**. The Registry domain is `https://uasd-production-752a45b0.up.railway.app`. Railway accepts public HTTPS on 443 and forwards requests to Gunicorn on 8000. [Railway public networking](https://docs.railway.com/networking/public-networking)

## Enroll a station

Run Monitor from the repository root. It uses the deployed Registry domain by default. Supply `--registry-url` to use a different HTTPS origin.

```sh
./WM_WS/WM_WS_M/build/WM_WS_M --ws-id 1 --monitor-location alicante \
  --registry-state-dir state/monitor-credentials
```

Or run Monitor in the existing station Compose project.

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 run --rm --build --no-deps \
  monitor ./build/WM_WS_M --ws-id 1 --monitor-location alicante \
  --registry-state-dir /var/lib/wm-ws/registry
```

In MINGW64, prefix the Compose command with `MSYS2_ARG_CONV_EXCL='*'` so the container paths pass through unchanged. For a native Windows build, use the `WM_WS_M.exe` path. [MSYS2 argument conversion](https://www.msys2.org/docs/filesystem-paths/)

Choose **Register**. Monitor sends its ID and city over HTTPS and saves the credential returned by Registry. Any client can register an unused ID. Registry rejects a second registration for an enrolled ID. **Refresh** checks the saved credential. **Deregister** revokes it and removes the local copy. Central's NAP integration still needs to verify the credential against Registry before secure activation can work.

If the station loses its saved credential, clear that enrollment from a shell in the running Registry deployment with `python -m wm_registry reset-enrollment --station-id 1`. The station can then register again. [Railway SSH](https://docs.railway.com/cli/ssh)

## HTTPS contract

The resource is `/api/registry/stations/{id}` on the Registry service's public HTTPS domain. `{id}` is the station's positive numeric ID.

| Method     | Bearer credential         | Request and response                                                                                                        |
| ---------- | ------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `PUT`    | None                      | JSON `{"city":"alicante"}` and `201` with `station_id`, `city`, and `credential`. An enrolled ID returns `409`. |
| `GET`    | Issued station credential | `200` with `station_id` and `city` for an active enrollment.                                                          |
| `DELETE` | Issued station credential | `204` after revocation.                                                                                                   |

Central can use `GET` with the credential once it receives it through NAP. Invalid input returns `400`, denied credentials return `401`, and an unavailable database returns `503`. Registry sends `Cache-Control: no-store` and logs the event, station, peer IP, time and result. On Railway the peer IP can be the proxy address.

## Tests

In an active Python 3.12 or newer environment, run:

```sh
python -m pip install -e WM_Registry
python -m unittest discover -s WM_Registry/tests -p 'test_registry.py'
python WM_Registry/tests/monitor_integration.py WM_WS/WM_WS_M/build/cli/WM_WS_M \
  --gui-driver WM_WS/WM_WS_M/build/gui/tests/monitor_registry_gui_driver
```

The integration test uses an existing terminal build and optionally the Qt test driver. Use the corresponding `.exe` paths on Windows. It starts a temporary HTTPS Registry using an OpenSSL test certificate.
