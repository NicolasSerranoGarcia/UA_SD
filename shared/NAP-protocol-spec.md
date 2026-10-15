# NAP Protocol Specification — Version 1.0

## About

This protocol is intended for use between Central and Monitor to authenticate watering stations and send status of them.

## General Message Format

```text
NAP#<protocol-version>#<ack-code>#<NAP-PC>[#fields]#FIN
```

| Component | Description |
| --- | --- |
|`NAP`|Name of the protocol. It is used as message initiator|
| `<protocol-version>` | Version of the NAP protocol used by the message. |
| `<ack-code>` | Unique code shared by a request and its response. For example, a response to an activation request with `<ack-code>` equal to `4` must also use `4`. |
| `<NAP-PC>` | Protocol code identifying the message type. |
| `[#fields]` | Optional, message-specific fields. |
| `FIN` | Message terminator. |

---

## Protocol Codes (NAP-PC)

| Code | Message Type |
| --- | --- |
| `0` | Registration |
| `1` | Activation |
| `2` | Status |
| `3` | ACK (success response) |
| `4` | NACK (error response) |

---

## Requests

### Registration

The monitor sends a registration request to the central server so that the server
can register the station in its database.

```text
NAP#<protocol-version>#<ack-code>#0#<station-id>#<location>#FIN
```

| Field | Description |
| --- | --- |
| `<station-id>` | Station identifier. |
| `<location>` | A consecutive sequence of characters that does not contain `#`. |

<br>

### Activation

```text
NAP#<protocol-version>#<ack-code>#1#<station-id>#FIN
```

| Field | Description |
| --- | --- |
| `<station-id>` | Station identifier. |

<br>

### Status

---

## Responses

### ACK: Success Response

If the request is fulfilled successfully, the server responds with the following
message:

```text
NAP#<protocol-version>#<ack-code>#3#FIN
```

The `<ack-code>` matches the code in the corresponding request.

<br>

### NACK: Error Response

If an error occurs, the server sends the client the following message with the
appropriate [NACK error code](#nack-error-codes):

```text
NAP#<protocol-version>#<ack-code>#4#<error-code>#FIN
```

<br>

### NACK Error Codes

| Code | Error | Description |
| --- | --- | --- |
| `0` | Syntax error | The message could not be parsed. |
| `1` | Station ID already exists | The station is already registered in the database. |
| `2` | Unknown station requested activation | The given station is not registered but has requested activation |
| `3` | Unknown NAP-PC | The NAP Protocol Code is a not known type |
