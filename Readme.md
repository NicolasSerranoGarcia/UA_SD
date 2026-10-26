# UA-SD
This repository containts all the resources related to the Practice 2 of Distributed Systems

## Shared protocols


### NAP protocol

This protocol is used to authenticate the watering stations with central. The specification for the protocol can be found [here](shared/NAP-protocol-spec.md).

![](shared/NAP-flow-diagram.png)


<br>

## Kafka
This section explains the structure and distribution of the topics created in Kafka.

<img width="968" height="1337" alt="image" src="https://github.com/user-attachments/assets/a2bd0de2-5221-404b-885e-39c6e64f1935" />



### Telemetry
This topic is used for all the data that the engine sends to the central regarding to the irrigation measurements (flow, accum volume...). It operates via key-based hash addressing.
| `Producer` | `Consumers` |
|---|---|
| Watering station | Central, Field Operators |
<br>

### FO-Req
This topic is the one the operators use to send operation commands to the central — such as start an irrigation, block an station or activate an station.
| `Producer` | `Consumer` |
|---|---|
| Field Operator | Central |
<br>

### Central-notis
This topic is used by the central to send notifications to the field operators, such as operation confirmations or station status updates
| `Producer` | `Consumer` |
|---|---|
| Central | Field Operator |
<br>

### WS-finish-conf
This topic is used by the stations to send confirmation to the central unit that irrigation has finished. The message will also contain a summary of the irrigation data.
| `Producer` | `Consumer` |
|---|---|
| Watering Station | Central |
<br>

### Central-ops
This topic is used by the central unit to send operational commands, such as locking a station or starting irrigation for a station, provided that the station is available. This commands can be sent the central unit or by a field operator via central.
| `Producer` | `Consumer` |
|---|---|
| Central | Watering Station |
<br>

### WS-start-conf
This topic is used by the stations to send confirmation or rejection of each operation previously sent by the central unit. This means that, via this topic, the station will confirm to the central unit all operations received — whether they were initiated by the central unit itself or by a field operator.
| `Producer` | `Consumer` |
|---|---|
| Watering Station | Central |
<br>
