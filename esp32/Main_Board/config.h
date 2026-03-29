// Config.h
#pragma once

// ---------------------------------------------------------------------------
// Shared — used by both Main_Board and Support_Board
// ---------------------------------------------------------------------------
#define FLOOR_ID      "Floor_1"   // Must match the Firebase path key
#define MAX_CAPACITY  120         // Max people this floor can hold

// ---------------------------------------------------------------------------
// Main Board only — not needed on Support_Board
// ---------------------------------------------------------------------------
#define WIFI_SSID      "Mahi"
#define WIFI_PASSWORD  "Mahinur1"

// Firebase Realtime Database
// Found in Firebase Console → Project Settings → Service Accounts → Database Secret
// or use the RTDB URL with no auth if your rules are open (not recommended)
#define FIREBASE_HOST  "usf-library-tracker-default-rtdb.firebaseio.com"   // e.g. "usf-library.firebaseio.com"
#define FIREBASE_AUTH  ""             // Legacy secret or auth token
