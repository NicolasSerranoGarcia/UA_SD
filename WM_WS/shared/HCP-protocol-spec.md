# HCP Protocol Specification — Version 1.2

HCP (Health Check Protocol) lets a station's Monitor check whether its Engine
is healthy and convey a successful NAP activation. Irrigation operations arrive
through Kafka using the JSON formats in the repository README.

## Connection

- Transport: TCP, with a persistent connection between one Monitor and its Engine.
- Engine is the server and listens on the configurable HCP port (default: `8080`).
- Monitor is the client and connects to the Engine's address and HCP port.

## Messages

Before health checks, Monitor sends `ID <ws-id>\n` once per connection.
The ID comes from its mandatory `--ws-id` option and contains 1–64 ASCII
letters, digits, hyphens, or underscores. The newline terminates the frame;
no null terminator is sent. Engine stores the ID and acknowledges with `OK`.
An invalid ID receives `KO` and the connection is closed. An incomplete frame
or disconnected peer cannot complete identification. `OK` here acknowledges
identification only, not health. Monitor must wait for it before sending `HC`.
Engine has no WS-ID command-line option; it obtains its identity from Monitor.
After the first identification, reconnecting with another ID is rejected until
Engine restarts. This keeps the Kafka partition and local checkpoint tied to one WS.

Example: `WM_WS_M --ws-id station-7 --engine-ip 127.0.0.1`.

After identification, every message is exactly **two ASCII bytes**, with no
newline, separator, or null terminator.

| Direction | Bytes | Meaning |
| --- | --- | --- |
| Monitor → Engine | `HC` | Request the current health status. |
| Monitor → Engine | `AC` | Apply activation after Central acknowledges NAP activation. |
| Engine → Monitor | `OK` | Healthy (`true`). |
| Engine → Monitor | `KO` | Fault active (`false`). |

For `AC`, `OK` means activation was applied; `KO` means it was refused. Identification
and an ordinary healthy response do not grant activation. `AC` cannot clear a
deliberate Central block; that requires an `ACTIVATE` operation on `central-ops`.

Engine responds using `Engine::checkHealth()`. A healthy Engine may be idle,
watering, or blocked by CENTRAL; a block alone is not a health fault.
Checking health does not start, stop, or repair the Engine. Fault handling
must stop irrigation independently of the next health check.

The HCP health response also latches a newly detected leak until one `HC` has
reported `KO`. Repair between consecutive polls cannot silently skip the fault
notification and required reactivation; `isHealthy()` remains the current sensor
state, while `checkHealth()` supplies this latched wire response.

## Exchange and failures

1. Monitor sends `HC` once per second while no previous request is outstanding.
2. Engine replies immediately with `OK` or `KO`.
3. Monitor waits at most two seconds for a complete response. This is the
   default response deadline (`--hcp-timeout-ms 2000`), also used separately for
   connection establishment and each send/identification operation. Partial reads
   do not restart it. Monitor uses nonblocking sockets and steady-clock deadlines.
4. On `KO`, Monitor reports the fault to CENTRAL through NAP, closes that NAP
   connection after its acknowledgement, and continues checking on HCP.
5. On timeout, connection failure, or invalid response, Monitor reports the
   health-check failure to CENTRAL, closes the connection, and retries connecting
   once per second. No response must never be interpreted as healthy.
6. At startup and after fault/communication recovery, a healthy response causes
   a fresh NAP activation exchange. Only after Central acknowledges it does Monitor
   send `AC`. NAP failure never grants activation.

Healthy NAP status reports reuse a persistent connection, allowing Central to
detect loss of Monitor. Engine detects HCP EOF, invalid requests, and receive
timeouts, stops irrigation, revokes activation, and disables Kafka publication.
Reconnecting alone does not restore permission to start or publish. Final water
measurements remain available for reporting after activation is restored.

Engine closes the connection if it receives an unknown request. Closing a failed
connection prevents a late reply from being mistaken for a reply to a new request.
There is at most one outstanding request, so no request ID or extra ACK is needed.

TCP is a byte stream: a send or receive call does not define a message boundary.
Send all two bytes and accumulate received bytes until a full two-byte message
is available, retaining any remaining bytes for the next message. A timeout
covers the whole response, including partial reads.

Both images require `WM_WS` as their Docker build context for the shared header:

```sh
docker build -f WM_WS/WM_WS_E/Dockerfile -t wm-ws-e WM_WS
docker build -f WM_WS/WM_WS_M/Dockerfile -t wm-ws-m WM_WS
```
