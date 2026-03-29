// Config.h
#pragma once

// ---------------------------------------------------------------------------
// Shared — used by both Main_Board and Support_Board
// ---------------------------------------------------------------------------
#define FLOOR_ID      "Floor_1"
#define MAX_CAPACITY  100

// WiFi channel your router uses
// Flash Main_Board first, check Serial for "[WiFi] Channel: X", put that number here
#define WIFI_CHANNEL  6          // ← replace with your actual channel

// MAC address of the Main Board for this floor
// Flash Get_MAC_Address.ino to the Main Board and read Serial output 68:FE:71:0B:F3:7C
uint8_t MAIN_BOARD_MAC[] = { 0x68, 0xFE, 0x71, 0x0B, 0xF3, 0x7C };

