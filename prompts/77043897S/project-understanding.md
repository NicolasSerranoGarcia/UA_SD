# Project requirements and architecture

User messages are transcribed from this conversation. Assistant responses are summaries, not verbatim quotations. IDs preserve the original conversation order across files.

## 01

**User — original prompt**

> can you help me understand how to kickstart the project given the project description pdf?

**Assistant — response summary**

Read practice-description.pdf and described the four required applications, their socket/Kafka connections, SQLite storage, milestones, deployment requirements, and assignment ambiguities. Suggested starting with one station and its health checks. The referenced attachment was practice-description.pdf.

## 02

**User — original prompt**

> I dont understand the specific of each. Everything thats not the queue must be coded in a languaje? so for example central must coded in c++ for example and uses a client for sqlite, the monitors use the native sockets to connect to central, a c++ client for kafka and so?

**Assistant — response summary**

Explained that students write Central, Monitor, Engine, and FO, while Kafka and SQLite are existing dependencies. Monitor uses sockets; Engine uses sockets and Kafka. SQLite is an embedded library and file rather than a separate server.

## 03

**User — original prompt**

> If the system is designed correctly, I could focus on writing the water system in C++ using a kafka client (confluence) and the native socket api. Other teammates can develop central and the field operators in other languajes

**Assistant — response summary**

Confirmed that modules can use different languages if their message contracts agree. Discussed socket framing, Kafka topics, IDs, units, and responsibilities. Corrected the name to Confluent and noted the brief limits groups to two people.

## 07

**User — original prompt**

> but isn't it better to add an sql service and let central connect to the public endpoint it provides?

**Assistant — response summary**

Compared SQLite with PostgreSQL/MySQL server architectures. Reiterated that the assignment requires SQLite and only Central needs direct database access. Explained that a separate cloud database would normally use private networking.
