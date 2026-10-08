# HCP Protocol Specification — Version 1.0

HCP (Health Check Protocol) lets a station's Monitor check whether its Engine
is healthy. It carries a boolean health result, not irrigation commands.

## Connection

- Transport: TCP, with a persistent connection between one Monitor and its Engine.
- Engine is the server and listens on the configurable HCP port (default: `8080`).
- Monitor is the client and connects to the Engine's address and HCP port.

## Messages

Every message is exactly **two ASCII bytes**, with no newline, separator, or
null terminator.

| Direction | Bytes | Meaning |
| --- | --- | --- |
| Monitor → Engine | `HC` | Request the current health status. |
| Engine → Monitor | `OK` | Healthy (`true`). |
| Engine → Monitor | `KO` | Fault active (`false`). |

Engine responds using `Engine::isHealthy()`. A healthy Engine may be idle,
watering, or blocked by CENTRAL; a block alone is not a health fault.
Checking health does not start, stop, or repair the Engine. Fault handling
must stop irrigation independently of the next health check.

## Exchange and failures

1. Monitor sends `HC` once per second while no previous request is outstanding.
2. Engine replies immediately with `OK` or `KO`.
3. Monitor waits at most three seconds for a complete response. This is the
   default HCP response timeout; it may be configured independently of the interval.
4. On `KO`, Monitor reports the fault to CENTRAL and continues checking.
5. On timeout, connection failure, or invalid response, Monitor reports the
   health-check failure to CENTRAL, closes the connection, and retries connecting
   once per second. No response must never be interpreted as healthy.
6. After a fault or communication failure, a new `OK` lets Monitor report recovery.

Engine closes the connection if it receives an unknown request. Closing a failed
connection prevents a late reply from being mistaken for a reply to a new request.
There is at most one outstanding request, so no request ID or extra ACK is needed.

TCP is a byte stream: a send or receive call does not define a message boundary.
Send all two bytes and accumulate received bytes until a full two-byte message
is available, retaining any remaining bytes for the next message. A timeout
covers the whole response, including partial reads.
