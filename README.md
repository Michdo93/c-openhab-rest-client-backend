# c-openhab-rest-client-backend

HTTP backend for the [c-openhab-rest-client test app](https://github.com/Michdo93/c-openhab-rest-client-test-app):
a browser cannot run C, so this small server calls
[c-openhab-rest-client](https://github.com/Michdo93/c-openhab-rest-client) 2.0.0 on its behalf.
Every function of the library is reachable – 153 REST functions and 25 event streams.

| Endpoint | Body / query | Answer |
|---|---|---|
| `GET /` | – | `{"status":"ok","library":"c-openhab-rest-client","version":"2.0.0"}` |
| `GET /api/methods` | – | all functions with parameters (`methods.json`) |
| `POST /api/connect` | `{url, token}` or `{url, username, password}` | `{loggedIn, isCloud, url}` |
| `POST /api/call` | `{url, token, class, method, args: {...}}` | result of the library function |
| `GET /api/events/stream` | `?url&token&class&method&args=<json>` | Server-Sent Events |

`class` and `method` are the names of python-openhab-rest-client, `args` holds the parameters by
name:

```bash
curl -s localhost:8080/api/call -d '{"url":"http://openhab:8080","token":"oh.xxx",
  "class":"Items","method":"sendCommand","args":{"itemName":"Wohnzimmer_Licht","command":"ON"}}'
# {"message":"OK"}
```

Results are exactly those of the library: JSON, text as JSON string, `{"message":"OK"}`,
errors as `{"error":"..."}`. Without `class` the event stream is `Events.getEvents` with
`topics` (e.g. `openhab/items/*/statechanged`).

The backend keeps no credentials; they come with every request. Run it only where you would
also run openHAB clients – everybody who reaches it can use it as a proxy.

## Build and run

```bash
cmake -S . -B build && cmake --build build -j
PORT=8080 ./build/openhab_backend          # reads methods.json from the working directory
```

Requirements: C11/C++17 compiler, CMake 3.16+, libcurl; c-openhab-rest-client, cJSON and
cpp-httplib are fetched by CMake. Docker: `docker build -t c-openhab-backend .` – `render.yaml`
deploys it on Render.

## Regenerating the call table

`src/calls.c` and `methods.json` are generated from python-openhab-rest-client:

```bash
python3 tools/gen_calls.py ../c-openhab-rest-client/tools/py2c.py \
    ../python-openhab-rest-client/openhab src/calls.c methods.json
```

## License

MIT – see [LICENSE](LICENSE).
