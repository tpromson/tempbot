// Persistent settings and small metadata files.

int loadDroppedCount() {
  if (!LittleFS.exists(DROPPED_FILE)) return 0;
  File f = LittleFS.open(DROPPED_FILE, "r");
  if (!f) return 0;
  int n = f.readStringUntil('\n').toInt();
  f.close();
  return n;
}

void saveDroppedCount(int count) {
  File f = LittleFS.open(DROPPED_FILE, "w");
  if (f) { f.println(String(count)); f.close(); }
}

String loadLastOtaNotify() {
  if (!LittleFS.exists(OTA_NOTIFY_FILE)) return "";
  File f = LittleFS.open(OTA_NOTIFY_FILE, "r");
  if (!f) return "";
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  return s;
}

void saveLastOtaNotify(const String& pair) {
  File f = LittleFS.open(OTA_NOTIFY_FILE, "w");
  if (f) { f.println(pair); f.close(); }
}

void saveConfig() {
  File f = LittleFS.open("/config.bin", "w");
  if (!f) return;
  f.write((uint8_t*)webAppUrl,         sizeof(webAppUrl));
  f.write((uint8_t*)timerDelayStr,     sizeof(timerDelayStr));
  f.write((uint8_t*)lineToken,         sizeof(lineToken));
  f.write((uint8_t*)minTempAlert,      sizeof(minTempAlert));
  f.write((uint8_t*)maxTempAlert,      sizeof(maxTempAlert));
  f.write((uint8_t*)lineGroupId,       sizeof(lineGroupId));
  f.write((uint8_t*)boardName,         sizeof(boardName));
  f.write((uint8_t*)bitmapName,        sizeof(bitmapName));
  f.write((uint8_t*)staticIP,          sizeof(staticIP));
  f.write((uint8_t*)minHumidAlert,     sizeof(minHumidAlert));
  f.write((uint8_t*)maxHumidAlert,     sizeof(maxHumidAlert));
  f.write((uint8_t*)otaPassword,       sizeof(otaPassword));
  f.write((uint8_t*)otaVersionUrl,     sizeof(otaVersionUrl));
  f.write((uint8_t*)otaBinUrl,         sizeof(otaBinUrl));
  f.write((uint8_t*)tempCalibrationStr,sizeof(tempCalibrationStr));
  f.write((uint8_t*)apiKey,            sizeof(apiKey));
  f.close();
}

void loadConfig() {
  if (!LittleFS.exists("/config.bin")) return;
  File f = LittleFS.open("/config.bin", "r");
  if (!f) return;
  size_t sz = f.size();
  f.readBytes(webAppUrl,     sizeof(webAppUrl));
  f.readBytes(timerDelayStr, sizeof(timerDelayStr));
  if (sz >= 840) {
    // The original unified layout is 850 bytes. The API key is appended so
    // existing devices keep their configuration after a firmware update.
    f.readBytes(lineToken,          sizeof(lineToken));
    f.readBytes(minTempAlert,       sizeof(minTempAlert));
    f.readBytes(maxTempAlert,       sizeof(maxTempAlert));
    f.readBytes(lineGroupId,        sizeof(lineGroupId));
    f.readBytes(boardName,          sizeof(boardName));
    f.readBytes(bitmapName,         sizeof(bitmapName));
    f.readBytes(staticIP,           sizeof(staticIP));
    f.readBytes(minHumidAlert,      sizeof(minHumidAlert));
    f.readBytes(maxHumidAlert,      sizeof(maxHumidAlert));
    f.readBytes(otaPassword,        sizeof(otaPassword));
    f.readBytes(otaVersionUrl,      sizeof(otaVersionUrl));
    f.readBytes(otaBinUrl,          sizeof(otaBinUrl));
    f.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
    if (sz >= 915) f.readBytes(apiKey, sizeof(apiKey));
    else apiKey[0] = '\0';
  } else if (sz >= 820) {
    f.readBytes(lineToken,          sizeof(lineToken));
    f.readBytes(minTempAlert,       sizeof(minTempAlert));
    f.readBytes(maxTempAlert,       sizeof(maxTempAlert));
    f.readBytes(lineGroupId,        sizeof(lineGroupId));
    f.readBytes(boardName,          sizeof(boardName));
    f.readBytes(bitmapName,         sizeof(bitmapName));
    f.readBytes(staticIP,           sizeof(staticIP));
    f.readBytes(otaPassword,        sizeof(otaPassword));
    f.readBytes(otaVersionUrl,      sizeof(otaVersionUrl));
    f.readBytes(otaBinUrl,          sizeof(otaBinUrl));
    f.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
    strcpy(minHumidAlert, "30.0");
    strcpy(maxHumidAlert, "80.0");
    apiKey[0] = '\0';
  } else if (sz >= 550) {
    f.readBytes(lineToken,     sizeof(lineToken));
    f.readBytes(minTempAlert,  sizeof(minTempAlert));
    f.readBytes(maxTempAlert,  sizeof(maxTempAlert));
    f.readBytes(lineGroupId,   sizeof(lineGroupId));
    f.readBytes(boardName,     sizeof(boardName));
    f.readBytes(bitmapName,    sizeof(bitmapName));
    f.readBytes(staticIP,      sizeof(staticIP));
    f.readBytes(minHumidAlert, sizeof(minHumidAlert));
    f.readBytes(maxHumidAlert, sizeof(maxHumidAlert));
    f.readBytes(otaPassword,   sizeof(otaPassword));
    f.readBytes(otaVersionUrl, sizeof(otaVersionUrl));
    f.readBytes(otaBinUrl,     sizeof(otaBinUrl));
    f.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
    apiKey[0] = '\0';
  } else if (sz >= 472) {
    f.readBytes(lineToken,    sizeof(lineToken));
    f.readBytes(minTempAlert, sizeof(minTempAlert));
    f.readBytes(maxTempAlert, sizeof(maxTempAlert));
    f.readBytes(lineGroupId,  sizeof(lineGroupId));
    f.readBytes(boardName,    sizeof(boardName));
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
    apiKey[0] = '\0';
  } else if (sz >= 452) {
    f.readBytes(lineToken,    sizeof(lineToken));
    f.readBytes(minTempAlert, sizeof(minTempAlert));
    f.readBytes(maxTempAlert, sizeof(maxTempAlert));
    f.readBytes(lineGroupId,  sizeof(lineGroupId));
    f.readBytes(boardName,    sizeof(boardName));
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
    apiKey[0] = '\0';
  } else if (sz >= 420) {
    f.readBytes(lineToken,    sizeof(lineToken));
    f.readBytes(minTempAlert, sizeof(minTempAlert));
    f.readBytes(maxTempAlert, sizeof(maxTempAlert));
    f.readBytes(lineGroupId,  sizeof(lineGroupId));
    boardName[0] = '\0';
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
    apiKey[0] = '\0';
  } else {
    lineToken[0] = '\0'; lineGroupId[0] = '\0'; boardName[0] = '\0';
    apiKey[0] = '\0';
    strcpy(minTempAlert, "20.0"); strcpy(maxTempAlert, "35.0");
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
  }
  f.close();
  Serial.printf("Config loaded (%d bytes): URL=%s delay=%s\n", (int)sz, webAppUrl, timerDelayStr);
  Serial.printf("Calibration: '%s' -> offset=%f\n", tempCalibrationStr, getTempCalibrationOffset());
}
