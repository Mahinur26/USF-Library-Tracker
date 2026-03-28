**Library Floor Occupancy Tracker**

Product Requirements Document

Version 1.0 • March 2026

  ---------------- ------------------------------------------------------
  **Project**      Library Floor Occupancy Tracker

  **Status**       Draft

  **Author**       TBD

  **Last Updated** March 2026

  **Tech Stack**   Next.js, Firebase Realtime DB, ESP32 (Arduino)
  ---------------- ------------------------------------------------------

1\. Project Overview

This system tracks real-time occupancy on each floor of a library using
a distributed network of ESP32 microcontrollers equipped with IR
break-beam sensors. Occupancy data is aggregated per floor, sent to a
cloud backend on a fixed interval, and displayed on a web frontend that
updates in real time using Firebase\'s onValue() listener.

2\. Goals & Non-Goals

2.1 Goals

-   Track the number of people entering and exiting each floor via IR
    break-beam sensor pairs.

-   Aggregate data from multiple sensor nodes per floor using the
    ESP-NOW wireless protocol.

-   Securely transmit floor occupancy data to a Firebase Realtime
    Database via a Next.js API route.

-   Display per-floor occupancy as a live percentage (current / max
    capacity) on a web frontend updated in real time via Firebase
    listeners.

-   Support multi-floor deployment with a consistent, reusable hardware
    module pattern.

2.2 Non-Goals

-   Identifying or tracking individual people (no biometric or personal
    data).

-   Camera-based or computer-vision occupancy detection.

-   Mobile app interface (web only for v1).

-   Historical analytics or trend dashboards (v1 focuses on live data
    only).

3\. Tech Stack

  ------------------------------------------------------------------------
  **Layer**             **Technology**        **Justification**
  --------------------- --------------------- ----------------------------
  Frontend + API        Next.js (React)       Unified codebase for UI and
                                              API routes; easy deployment
                                              on Vercel

  Realtime Database     Firebase Realtime     Low-latency onValue()
                        Database              listeners eliminate polling;
                                              free tier sufficient for
                                              small libraries

  Microcontroller       ESP32 (Arduino        Built-in WiFi, ESP-NOW
                        framework)            support, ample I/O for dual
                                              IR sensors

  Wireless Protocol     ESP-NOW               Low-latency peer-to-peer
                                              comms between ESP32s; no
                                              router required on the floor

  Sensor                IR break-beam sensor  Directional entry/exit
                        pairs                 detection using two beams in
                                              sequence
  ------------------------------------------------------------------------

4\. System Architecture

Each floor consists of one Main Board (initiator) and zero or more
Support Boards (responders). All boards on a floor are connected via
ESP-NOW. The Main Board aggregates entry/exit counts from all nodes and
POSTs to a Next.js API route once per minute over WiFi. The API route
validates the payload and writes to Firebase. The frontend subscribes to
Firebase using onValue() and re-renders instantly on any change.

4.1 Data Flow

-   IR sensors on each board detect entry/exit direction and update
    local counters.

-   Support Boards broadcast delta payloads to the Main Board via
    ESP-NOW.

-   Main Board accumulates all deltas (local + received) and POSTs to
    /api/update-floor every 60 seconds.

-   Next.js API route validates and writes { current_occupancy,
    total_entries, total_exits } to Firebase.

-   Frontend onValue() listener receives the write and re-renders the
    occupancy display immediately.

4.2 Firebase Data Model

Each floor stores three fields to support both live display and drift
detection:

  ------------------------------------------------------------------------
  **Field**           **Type**    **Description**
  ------------------- ----------- ----------------------------------------
  current_occupancy   Integer     Live count of people currently on the
                                  floor (entries - exits since last reset)

  total_entries       Integer     Cumulative entries since last daily
                                  reset. Used for drift auditing.

  total_exits         Integer     Cumulative exits since last daily reset.
                                  Used for drift auditing.

  max_capacity        Integer     Configured maximum capacity for the
                                  floor. Set manually. Used to compute %.

  last_updated        Timestamp   Unix timestamp of the last write from
                                  the Main Board.
  ------------------------------------------------------------------------

Path structure: floors/{floor_id}/current_occupancy, etc. The floor_id
is a short string set in Config.h (e.g., \"floor_1\", \"floor_2\").

5\. Arduino Modules

5.1 Get_MAC_Address.ino

A one-time utility sketch. Flash to any ESP32 to print its MAC address
over Serial. Used during setup to populate Config.h with the MAC address
of each Main Board so Support Boards know their target peer.

  -----------------------------------------------------------------------
  **Detail**             **Value**
  ---------------------- ------------------------------------------------
  Purpose                Discover ESP32 MAC addresses before deployment

  Output                 Serial print of MAC address at 115200 baud

  Run frequency          Once per device during initial setup
  -----------------------------------------------------------------------

5.2 Config.h (shared header)

A shared configuration header included by both Main_Board.ino and
Support_Board.ino. Centralizes all deployment-specific values so
credentials and IDs are never hardcoded in .ino files.

-   FLOOR_ID --- string identifier matching the Firebase path (e.g.,
    \"floor_1\")

-   WIFI_SSID / WIFI_PASSWORD --- network credentials for the Main Board

-   API_ENDPOINT --- full URL of the Next.js API route

-   MAIN_BOARD_MAC --- MAC address of the Main Board for this floor
    (used by Support Boards to target ESP-NOW messages)

-   MAX_CAPACITY --- used to guard against nonsensical reads; Main Board
    rejects any floor count exceeding this

5.3 Main_Board.ino (Initiator)

Flashed onto one ESP32 per floor. This board is the data aggregator and
the only WiFi-connected node on the floor.

Responsibilities

-   Read two IR break-beam sensors to detect directional foot traffic
    (beam 1 then beam 2 = entry; beam 2 then beam 1 = exit).

-   Maintain local counters: entries and exits (not a single net delta).

-   Receive ESP-NOW packets from Support Boards containing their
    entry/exit deltas and add them to the floor totals.

-   Every 60 seconds, POST { floor_id, entries, exits } to the Next.js
    API route over WiFi.

-   Reset local delta counters after a successful POST (cumulative
    totals remain for audit).

IR Direction Detection Logic

Two sensors are mounted in sequence across the doorway (Sensor A closer
to the corridor, Sensor B closer to the floor). The order of interrupts
determines direction:

-   A breaks first, then B --- person is entering the floor.

-   B breaks first, then A --- person is exiting the floor.

A debounce window of \~200ms is applied per sensor pair to avoid
double-counting.

ESP-NOW Reception

The Main Board registers a receive callback. Incoming packets from
registered Support Board MACs contain a small struct: { int16_t
entries_delta, int16_t exits_delta }. The Main Board adds these to its
running totals immediately on receipt.

5.4 Support_Board.ino (Responder)

Flashed onto all additional ESP32s on the same floor. These boards have
no WiFi role --- they communicate only via ESP-NOW.

Responsibilities

-   Read two IR break-beam sensors using the same directional detection
    logic as the Main Board.

-   Accumulate local entry/exit deltas since the last transmission.

-   Every 60 seconds, send a packet containing { entries_delta,
    exits_delta } to the Main Board MAC via ESP-NOW.

-   Reset local delta counters after a successful send.

Note: Support Boards do not need WiFi credentials. They only need
MAIN_BOARD_MAC from Config.h.

6\. Backend --- Next.js API Route

6.1 POST /api/update-floor

Receives the floor occupancy update from the Main Board and writes to
Firebase. Keeping this layer server-side ensures Firebase credentials
are never embedded in firmware.

  -----------------------------------------------------------------------
  **Property**       **Detail**
  ------------------ ----------------------------------------------------
  Method             POST

  Auth               Pre-shared API key in request header (X-API-Key).
                     Main Board includes this from Config.h.

  Request body       { floor_id: string, entries: number, exits: number }

  Validation         Reject negative values; reject floor_id not in an
                     allowlist; reject entries/exits exceeding
                     MAX_CAPACITY

  Firebase write     Compute current_occupancy = existing + (entries -
                     exits), cap at 0 (floor cannot go negative). Update
                     total_entries, total_exits, last_updated.

  Response           200 OK on success; 400/401/500 with error message on
                     failure.
  -----------------------------------------------------------------------

7\. Frontend

7.1 Display Requirements

-   One card per floor showing floor name, current occupancy percentage
    (current_occupancy / max_capacity), and a visual fill bar.

-   Color coding: green below 50% capacity, amber from 50--79%, red at
    80% and above.

-   Occupancy percentage label (e.g., \"42% --- Moderate\") displayed
    alongside the bar.

-   \"Last updated\" timestamp shown per floor so users know the data
    age.

7.2 Realtime Updates

The frontend uses Firebase\'s onValue() listener (not polling). The
listener fires immediately on page load with the current snapshot, and
again on every Firebase write. This means the display refreshes the
moment the Main Board\'s 60-second POST lands in Firebase --- no
client-side timer is needed.

7.3 Threshold UX (frontend-driven)

Rather than having the hardware send extra writes on threshold
crossings, the frontend handles threshold-based UI changes locally. When
a floor\'s occupancy crosses a configured threshold (e.g., 50%, 80%),
the UI updates its color and label instantly because the onValue()
listener already provides the latest value. No additional hardware
complexity is required.

8\. Deployment & Operations

8.1 Daily Occupancy Reset

current_occupancy, total_entries, and total_exits should be reset to 0
at library open time each day. Implement this as either a Next.js cron
endpoint (called by Vercel Cron or an external scheduler at opening
time) or a Firebase Cloud Function triggered on a schedule. This
prevents occupancy drift from accumulating across days.

8.2 Hardware Setup Per Floor

-   Step 1: Flash Get_MAC_Address.ino to each ESP32 and record MAC
    addresses.

-   Step 2: Populate Config.h with floor_id, WiFi credentials, API
    endpoint, and Main Board MAC.

-   Step 3: Flash Main_Board.ino to the designated initiator board.

-   Step 4: Flash Support_Board.ino to all remaining boards on the
    floor.

-   Step 5: Mount IR sensors in doorways. Sensor A faces the corridor;
    Sensor B faces the floor interior.

9\. Open Questions

  ----------------------------------------------------------------------------
  **\#**   **Question**                   **Notes**
  -------- ------------------------------ ------------------------------------
  1        What is the max capacity per   Needed in Config.h and Firebase
           floor?                         before go-live

  2        What happens when WiFi is      Main Board could buffer up to N
           unavailable?                   minutes of deltas in memory and
                                          flush when reconnected

  3        How is sensor drift handled    total_entries and total_exits allow
           long-term?                     periodic manual reconciliation

  4        Is a manual override / admin   Useful if a count goes negative due
           reset UI needed?               to sensor misfire

  5        Authentication on the          v1 can be public read; restrict
           frontend?                      write to the API key only
  ----------------------------------------------------------------------------