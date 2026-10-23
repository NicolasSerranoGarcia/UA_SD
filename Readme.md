# UA-SD
This repository containts all the resources related to the Practice 2 of Distributed Systems

## Shared protocols


### NAP protocol

This protocol is used to authenticate the watering stations with central. The specification for the protocol can be found [here](shared/NAP-protocol-spec.md).

![](shared/NAP-flow-diagram.png)


<br>

## Kafka
This section explains the structure and distribution of the topics created in Kafka.

<img width="968" height="1150" alt="image" src="https://github.com/user-attachments/assets/956865be-6f4a-47e7-9947-cff054771baf" />



### Telemetry
This topic is used for all the data that the engine sends to the central regarding to the irrigation measurements (flow, accum volume...). It operates via key-based hash addressing.
| `Producer` | `Consumers` |
|---|---|
| Watering station | Central, Field Operators |

### FO-Req
This topic is the one the operators use to send irrigation requests to the central
| `Producer` | `Consumer` |
|---|---|
| Field Operator | Central |

### Central-notis
This topis is used by the central to send notifications to the field operators, such as irrigation summaries or station status updates
| `Producer` | `Consumer` |
|---|---|
| Central | Field Operator |

### WS-finish-conf
This topic is the one the stations use to send confirmation of the completion of irrigation to the central unit. 
| `Producer` | `Consumer` |
|---|---|
| Watering Station | Central |

### Central-ops
This topic is used by the central unit to send operational commands, such as locking a station or starting irrigation for a station, provided that the station is available.
| `Producer` | `Consumer` |
|---|---|
| Central | Watering Station |

### WS-start-conf
This topic is the one that stations use to send confirmation/authorization to start irrigation to the central unit. This means that, through this topic, central will receive both the response to start irrigation, and the station's own irrigation self start.
| `Producer` | `Consumer` |
|---|---|
| Watering Station | Central |
