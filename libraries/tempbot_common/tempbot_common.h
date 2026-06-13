#ifndef TEMPBOT_COMMON_H
#define TEMPBOT_COMMON_H

#include <Arduino.h>
#include <time.h>

// ===== Extern Globals =====
// These must be defined in the main .ino file
extern char lineToken[200];
extern char lineGroupId[40];
extern char boardName[32];
extern char tempCalibrationStr[10];

// ===== Shared Functions =====

// Format epoch time as "HH:MM" or "HH:MM:SS"
String formatTime(time_t epoch, bool includeSeconds);

// URL-encode a string
String urlEncode(String str);

// Get the board identifier (boardName or "BOARD_<chipid>")
String getBoardIdentifier();

// Send a message via LINE Notify (no-op if token/group not set)
void sendLineNotify(String message);

// Get temperature calibration offset
float getTempCalibrationOffset();

// Compare semver "major.minor.patch": true if 'latest' is strictly newer than
// 'current'. Used by OTA so boards only ever upgrade, never downgrade.
bool isNewerVersion(String latest, String current);

#endif
