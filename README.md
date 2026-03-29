# USF Library Floor Occupancy Tracker

A real-time system that tracks how many people are on each floor of the library using ESP32 microcontrollers, IR break-beam sensors, and a Next.js + Firebase web dashboard.

---

## How It Works

Each floor has one **Main Board** and optionally one or more **Support Boards**. Each board reads two IR break-beam sensors mounted in a doorway to detect whether someone is entering or exiting based on which beam breaks first. Support Boards send their counts to the Main Board every 10 seconds via **ESP-NOW** (a peer-to-peer WiFi protocol that doesn't require a router). The Main Board aggregates all counts and writes the floor occupancy to **Firebase Realtime Database** every 10 seconds over HTTPS. The **Next.js frontend** subscribes to Firebase with an `onValue()` listener and updates the display the instant new data arrives.

```
IR Sensors → Support Board ──ESP-NOW──→ Main Board ──HTTPS──→ Firebase ──onValue()──→ Frontend
IR Sensors → Main Board ──────────────────────────────────────────────↗
```

---

## Project Structure

```
USF Library Tracker/
├── esp32/
│   ├── config/
│   │   └── Config.h               # All credentials and settings — edit this before flashing
│   ├── Get_MAC_Address/
│   │   └── Get_MAC_Address.ino    # One-time utility to read ESP32 MAC address
│   ├── Main_Board/
│   │   ├── Main_Board.ino         # Flash to the initiator board on each floor
│   │   └── Config.h               # Copy of Config.h
│   └── Support_Board/
│       ├── Support_Board.ino      # Flash to all other boards on the same floor
│       └── Config.h               # Copy of Config.h
└── library-occupancy-tracker/     # Next.js web app
    ├── src/
    │   ├── app/
    │   │   ├── page.tsx           # Frontend dashboard
    │   │   ├── layout.tsx
    │   │   └── api/
    │   │       └── update-floor/
    │   │           └── route.ts   # Optional API route (not used in current build)
    │   ├── lib/
    │   │   └── firebase.ts        # Firebase initialization
    │   └── components/
    │       └── FloorCard.tsx      # Per-floor occupancy card
    └── .env.local                 # Firebase credentials for Next.js (never commit)
```

---

## Firebase Data Structure

```
floors/
  Floor_1/
    current_occupancy: 42
    max_capacity:      100
    total_entries:     310
    total_exits:       268
    last_updated:      823042     ← ms since board boot
  Floor_2/
    ...
```

`current_occupancy` is clamped between `0` and `max_capacity`. `total_entries` and `total_exits` are cumulative since last board boot and are useful for detecting sensor drift over time.

---

## Hardware Setup

### Components needed per door
- 2× IR break-beam sensor pairs (emitter + receiver)
- 2x 1k Ohm resistors (for pull up resistor)
- 1× ESP32 development board
- Jumper wires

### Wiring
| Sensor | ESP32 Pin | Notes |
|--------|-----------|-------|
| Sensor A (corridor side) | GPIO 18 | `INPUT_PULLUP` — LOW when beam broken |
| Sensor B (floor side) | GPIO 19 | `INPUT_PULLUP` — LOW when beam broken |

Mount sensors in sequence across the doorway. **Sensor A must face the corridor (outside), Sensor B must face the floor interior (inside).** The order of which beam breaks first determines direction:
- A breaks first → **entry**
- B breaks first → **exit**

---

## Configuration

All settings live in `Config.h`. Copy this file into each sketch folder before flashing.

```cpp
// Config.h

// Shared — both Main Board and Support Board
#define FLOOR_ID      "Floor_1"   // Must match Firebase path key exactly (case-sensitive)
#define MAX_CAPACITY  120          // Max occupancy for this floor
#define WIFI_CHANNEL  1            // Must match your router's channel — see Setup below

// MAC address of the Main Board for this floor
// Get this by flashing Get_MAC_Address.ino and reading Serial output
uint8_t MAIN_BOARD_MAC[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// Main Board only
#define WIFI_SSID      "your_network"
#define WIFI_PASSWORD  "your_password"
#define FIREBASE_HOST  "your-project-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH  "your_database_secret"
```

### Finding FIREBASE_AUTH
Firebase Console → Project Settings → Service Accounts → **Database secrets** → Show or Add secret. This is a long random string, not the Web API Key (`AIzaSy...`).

### Finding WIFI_CHANNEL
Flash `Main_Board.ino` first. Check Serial Monitor — it will print `[WiFi] Channel: X`. Put that number in `WIFI_CHANNEL` in `Config.h` before flashing Support Boards.

---

## Flashing Instructions

> Arduino IDE with the ESP32 board package installed is required.  
> Install ESP32 boards: File → Preferences → add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Board Manager URLs, then Tools → Board → Boards Manager → search "esp32" by Espressif → Install.

### Required libraries (Tools → Manage Libraries)
- **ArduinoJson** by Benoit Blanchon

### Step 1 — Get MAC addresses
1. Open `Get_MAC_Address/Get_MAC_Address.ino`
2. Select board: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
3. Select port: **Tools → Port → (your ESP32's COM port)**
4. Upload → open Serial Monitor at 115200 baud
5. Record the MAC address printed — this is your Main Board's MAC
6. Repeat for any other boards if needed

### Step 2 — Update Config.h
Fill in all values in `Config.h`:
- Paste the Main Board MAC into `MAIN_BOARD_MAC`
- Add your WiFi credentials
- Add your Firebase host and auth secret
- Leave `WIFI_CHANNEL` as `1` for now (you'll confirm it after first flash)

Copy `Config.h` into both `Main_Board/` and `Support_Board/` folders.

### Step 3 — Flash Main Board
1. Open `Main_Board/Main_Board.ino`
2. Select the Main Board's COM port
3. Upload
4. Open Serial Monitor at 115200 baud
5. Note the channel printed: `[WiFi] Channel: X`
6. Update `WIFI_CHANNEL` in `Config.h` if it differs from what you set

### Step 4 — Flash Support Board(s)
1. Update `WIFI_CHANNEL` in `Config.h` to match what Main Board reported
2. Copy updated `Config.h` into `Support_Board/`
3. Open `Support_Board/Support_Board.ino`
4. Select the Support Board's COM port
5. Upload

### Verifying it works
Main Board Serial Monitor should show:
```
[WiFi] Connected. IP: ...
[ESP-NOW] Ready. Channel=1
[Boot] max_capacity write HTTP 200
[Boot] Main Board ready.
[ESP-NOW] Received — entries: 0  exits: 0   ← Support Board packets arriving
[Firebase] Write successful.
```

Support Board Serial Monitor should show:
```
[Boot] Support Board ready. Channel=1
[ESP-NOW] Packet queued. entries=0 exits=0
```

> Note: `onDataSent` may report FAILED on the Support Board even when data arrives correctly. This is a known ESP32 quirk — when the Main Board is connected to WiFi, it briefly hops channels for beacon frames, causing the ACK to be missed. As long as the Main Board shows `[ESP-NOW] Received`, the system is working correctly.

---

## Firebase Rules

For development, set your rules to open in Firebase Console → Realtime Database → Rules:

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

> Before going live in the library, tighten these rules to only allow writes authenticated with your database secret.

---

## Next.js Frontend

### Setup
```bash
cd library-occupancy-tracker
npm install
```

Create `.env.local` in the project root:
```env
NEXT_PUBLIC_FIREBASE_API_KEY=
NEXT_PUBLIC_FIREBASE_AUTH_DOMAIN=
NEXT_PUBLIC_FIREBASE_DATABASE_URL=
NEXT_PUBLIC_FIREBASE_PROJECT_ID=
```

All values are found in Firebase Console → Project Settings → General → Your apps → Firebase SDK snippet.

### Run
```bash
npm run dev
```

App runs at `http://localhost:3000`. The dashboard reads from Firebase in real time using `onValue()` — no polling needed. The display updates the instant the Main Board writes new data.

### Display
Each floor card shows:
- Current occupancy percentage (`current_occupancy / max_capacity`)
- Last updated timestamp

---

## Adjusting Update Intervals

| Setting | File | Variable | Default |
|---------|------|----------|---------|
| How often Main Board writes to Firebase | `Main_Board.ino` | `SEND_INTERVAL_MS` | `10000` (10s) |
| How often Support Board sends to Main Board | `Support_Board.ino` | `SEND_INTERVAL_MS` | `10000` (10s) |
| Sensor direction debounce window | Both | `DEBOUNCE_WINDOW_MS` | `500` (500ms) |

Keep Support Board interval shorter than Main Board interval so the Main Board always has fresh aggregated data before it writes to Firebase.

---

## Adding a New Floor

1. Get MAC address of the new floor's Main Board via `Get_MAC_Address.ino`
2. Create a new `Config.h` with the new `FLOOR_ID` (e.g. `"Floor_2"`) and the new board's MAC
3. Flash `Main_Board.ino` to the initiator board and `Support_Board.ino` to any responder boards
4. Firebase will auto-create the `floors/Floor_2/` node on first boot
5. Add the new floor to your frontend — add `"Floor_2"` wherever your floor list is defined

---

## Known Issues & Limitations

- `last_updated` stores milliseconds since board boot, not a real timestamp. For real timestamps, NTP time sync would need to be added to `Main_Board.ino`.
- Occupancy resets to 0 on board reboot. A daily scheduled reset (Firebase Cloud Function or Next.js cron) is recommended for production to prevent drift accumulating across days.
- If the library has more than one entrance per floor, one sensor pair per entrance is needed, each on its own board.
- `setInsecure()` is used for HTTPS — acceptable for an internal tool, but a proper root CA certificate should be used for a production deployment.