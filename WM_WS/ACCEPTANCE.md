# Watering station acceptance evidence

This matrix covers the watering-station requirements in `practice-description.pdf`,
`correction-guide.pdf`, the repository README, and the agreed WS behavior. It
distinguishes WS tests from checks that require the complete team deployment.

HCP remains **Engine server / Monitor client**. NAP 1.0 and the README Kafka JSON
formats are unchanged. `DC` is a WS-local HCP extension for Central availability.

## Requirement coverage

| Requirement | WS evidence | Remaining team/evaluation check |
| --- | --- | --- |
| Engine/Monitor roles and executable names | Separate native and Linux builds; independent-container checks | Launch the prescribed scenario in front of the evaluator |
| Station identity, registration, authentication | NAP acceptance/rejection process tests; request/station/checksum validation | Central registration database and visible denial messages |
| Runtime configuration without recompilation | CLI validation; numeric IP and hostname alternatives; Docker environment mappings | Configure the actual lab/cloud endpoints |
| Activate before starting | Engine eligibility tests; fresh NAP ACK followed by HCP AC | Central/FO authorization UI |
| Manual, FO, and Central control | Station IRRIGATION/BLOCK/ACTIVATE tests; local menu process tests | Initiate operations through real FO and Central |
| Block stops; external ACTIVATE required | Engine/Station tests; active leak rejects new BLOCK; repair preserves any earlier block; blocked and unblocked Central-outage process cases | Central Out of Service display and commands to all stations |
| Flow meter, valve, and supervision | Flow bounds, integrated-volume tests, immediate stop/fault tests | Observe the simulation during the grading run |
| One-second health checks | Monitor socket-peer and process tests on Windows and Linux | Observe real Central life-message handling |
| Leak, repair, and reactivation | Latched KO; repeated OK/KO on one NAP health connection; acknowledged repair restores HCP authorization; block preserved; no automatic resume | Central Leak display and notification to the affected FO |
| One-second live measurements | Terminal refresh and telemetry tests; same simulated flow model | Required real Central/FO displays |
| Duration expiry, local/Central STOP, final totals | Engine deadline, Station STOP without blocking, completion, original owner, duplicate/recovery and exact-schema tests | Final liters/time summary reaches the correct FO |
| Available/Watering/Leak/Out of Service/Disconnected | Engine/Monitor state guards and visible status; Out of Service displayed explicitly | Central colors, blinking, and state precedence |
| Monitor loss | Valve stops, activation revoked, publication suppressed; Docker peer-loss check | Central shows Disconnected even if Engine remains alive |
| Engine crash | Hard-kill process test recovers final totals from the journal | Central/FO receive and display the recovered result |
| FO crash | WS execution is independent of FO liveness | Kill FO during irrigation, then verify its result after recovery |
| Central outage | Full local socket/process tests: finish existing watering, reject new starts, recover without restarting WS | Repeat against real Central, including shutdown and restart |
| Transaction preservation | Write-ahead intent, cached operations, pending outbox, synced journal and recovery tests | Verify real Central accounting under replay |
| Concurrent operation | Separate workers; simultaneous console/health service; second socket cannot displace the active Monitor | Concurrent requests from multiple real FO instances |
| Recovery with minimal restarts | Native outage recovery; actual Docker Engine recreation at a new IP without restarting Monitor | Rehearse failures across the required machines |
| Recipient routing and collisions | Outgoing ID-modulo routing; Central-keyed command assignment; mock broker with default partitioning and recipient filtering | Two real FO/station pairs, including a shared partition |
| Kafka interruption and recovery | Mock-broker outage and retained final-summary delivery tests | Repeat with the deployed Kafka service |
| Storage failures | Invalid versions, corrupt slots, unwritable storage, and controlled service failure | Inspect the deployed persistent-volume configuration |
| Usable interfaces and controlled errors | Live metrics; input/backspace/Enter; visible Central status; fatal diagnostics; headless Qt tests | Screenshots and observation at the evaluator's terminal size |
| Independent Docker containers | Two station pairs, both startup orders, DNS change, volume reuse, peer loss; separate Compose run checks | Two lab machines/VMs, with the required cloud services already running |
| Small realistic flow variations | Bounded pressure model and integrated-volume tests | Observe live measurements |
| Platform behavior | Native Windows suites and Linux-container suites; NINTTP platform flags | Confirm the selected lab environment |
| Report, repository, and explainability | Reproducible commands in BUILDING.md; tests and protocol documentation | Report, screenshots, binaries, prompts, instructor access, and individual defense |

## Recovery guarantees

- Public Engine readings advance after the corresponding snapshot has been synced
  to the journal. Progress sampling has a nominal 100 ms cadence; displayed values
  are rounded to two decimal places.
- Valve closure does not wait for a disk write. Final measurements are persisted
  before the completion record is published.
- Restart recovers the interrupted irrigation's saved totals and operator without
  resuming watering or adding consumption from time spent offline.
- Pending messages use at-least-once delivery. A crash between broker acknowledgement
  and the local delivered checkpoint may replay a record. Central processing is not
  claimed to be exactly once.
- These are simulated-meter guarantees for committed/reported usage, not a claim
  to reconstruct physical water delivered between disk commits or to survive loss
  of the entire persistent volume.

## Reproducing the checks

Verified on 2026-10-28 against the implementation based on `032b198`:

| Environment | Result |
| --- | --- |
| Native Windows / MinGW64 | Engine 57, Monitor 26, process integration 22; all 105 passed |
| Linux containers | Engine 57, Monitor 26, process integration 22; all 105 passed |
| Native Windows Qt and Linux headless Qt | 38 passed on each platform, including the 26 Monitor backend tests |
| Independent Docker / Compose smoke checks | 13 passed |

These results establish WS behavior under local fixtures. They do not establish
the real Central/FO evaluation checks below.

Use the native, Docker unit-test, Linux process-test, and headless GUI commands in
[BUILDING.md](BUILDING.md). Unit/process fixtures use local sockets and mock Kafka;
they do not send test irrigation to the deployed cloud services.

The Docker smoke rehearsal used uniquely named test resources, loopback-only
Central/Kafka endpoints, two station IDs, independent containers and separate
Compose run commands. It verified hostname reconnection after an Engine IP change
and removed only its own test containers, networks, and volumes afterward.

## Continuous NAP health reporting (2026-10-29)

Monitor keeps the health connection open after an acknowledged `KO` and continues
sending health reports at the configured interval (default one second). Repair
returns to `OK` on the same socket. The initial NAP activation still uses a
separate connection, and HCP recovery authorization cannot clear a Central block.

Verification: 39 Monitor/backend GUI tests passed on native Windows and Linux;
22 native process tests passed; the Engine block/repair health regression passed.
These tests include repeated `KO`, quick repair, late recovery acknowledgement,
and same-socket recovery.

The bundled Central now keeps its health connection open after acknowledging
`KO`. Its completion and healthy-report handlers must also preserve administrative
blocking and avoid restoring an irrigation that the leak already ended. These
are complete-system state-precedence checks; WS tests alone do not establish them.

## Central/FO interface compatibility (2026-10-30)

Engine accepts FO's full unsigned 128-bit numeric UUID operation codes, keeps their
digits exact through command/reply checkpoints, and emits numeric confirmations.
Its command consumer follows Central's canonical station-key CRC32 partitioning;
WS-produced records retain recipient-modulo routing. Metadata supplies the counts.
Final `total_time` uses minutes while request durations and meter snapshots use
seconds. Checkpoint version 2 upgrades pending version 1/unversioned final totals
once, including recovery after an interrupted session.

Verification: all 76 Engine/Station/Journal/Service tests passed on native Windows
and isolated Linux, including the container's older Boost version. All 22 native
process tests passed. The mock-broker case uses Central's actual default key
partitioner on a partition different from ID modulo, and verifies an exact UUID
confirmation on the wire. Central and FO source were not changed or cloud-tested.

## Full-system evaluation gate

Do not mark the complete practice accepted from WS tests alone. Rehearse the real
Central, Kafka, SQLite, FO, and WS deployment on the required machines:

1. Register two stations and operate them locally, through FO, and through Central.
2. Verify ownership, one-second measurements, duration/stop, BLOCK/ACTIVATE, and
   final `ws-finish-conf` liters/time.
3. Kill Monitor, Engine, FO, and Central separately during irrigation and verify
   the guide's status matrix, retained results, and minimal restart behavior.
4. Interrupt Kafka/network access, recover, and verify pending delivery and Central
   accounting. Delayed telemetry must not override disconnected or completed state.
5. Add an independent station during operation and confirm partition/volume isolation.
6. Capture terminal/dashboard screenshots and record exact commands, IDs, versions,
   and results in the report. Validate instructor repository access and prompt evidence.

Final accumulated totals use the existing README `ws-finish-conf` fields. Live
dashboard calculations, Central colors, FO result recovery, and duplicate handling
are team integration checks; no additional WS payload fields are assumed here.
