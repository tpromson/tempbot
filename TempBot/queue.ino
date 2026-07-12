// Offline queue persistence and replay.

int getQueueSize() {
  if (!LittleFS.exists(QUEUE_FILE)) return 0;
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    ESP.wdtFeed();
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() > 2) count++;
  }
  f.close();
  return count;
}

#ifdef SENSOR_DHT22
void queueData(float temp, float humid) {
#else
void queueData(float temp) {
#endif
  int size = getQueueSize();
  if (size >= MAX_QUEUE_ENTRIES) {
    droppedEntries++; saveDroppedCount(droppedEntries);
    Serial.println("Queue full! Removing oldest entry...");
    File src = LittleFS.open(QUEUE_FILE, "r");
    File dst = LittleFS.open("/qtmp.csv", "w");
    if (src && dst) {
      src.readStringUntil('\n'); // skip oldest
      while (src.available()) {
        ESP.wdtFeed();
        String line = src.readStringUntil('\n'); line.trim();
        if (line.length() > 2) dst.println(line);
      }
    }
    src.close(); dst.close();
    LittleFS.remove(QUEUE_FILE); LittleFS.rename("/qtmp.csv", QUEUE_FILE);
    size--;
  }
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (f) {
    time_t now = time(nullptr);
    if (now < 1000000000 && lastSyncTimeEpoch >= 1000000000)
      now = lastSyncTimeEpoch + (millis() - lastSyncTimeMillis) / 1000;
#ifdef SENSOR_DHT22
    f.println(String(now) + "," + String(temp,1) + "," + String(humid,1));
#else
    f.println(String(now) + "," + String(temp,1));
#endif
    f.close();
    Serial.print("Queued. Size: "); Serial.println(size + 1);
  }
}

void flushQueue() {
  if (strlen(webAppUrl) < 10) return;
  if (!LittleFS.exists(QUEUE_FILE)) return;

  int entryCount = getQueueSize();
  if (entryCount == 0) { LittleFS.remove(QUEUE_FILE); return; }

  Serial.print("Flushing "); Serial.print(entryCount); Serial.println(" queued entries...");
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(5, 10); display.print("SYNCING OFFLINE DATA");
  display.setCursor(5, 25); display.print(entryCount); display.print(" buffered entries");
  display.display();

  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(4096, 1024);
  String boardID = getBoardIdentifier();
  int sentCount = 0;

  // Pass 1: stream line-by-line
  {
    File f = LittleFS.open(QUEUE_FILE, "r");
    while (f.available()) {
      ESP.wdtFeed();
      if (WiFi.status() != WL_CONNECTED) break;
      String line = f.readStringUntil('\n'); line.trim();
      if (line.length() <= 2) continue;

      int c1 = line.indexOf(',');
      if (c1 < 0) { sentCount++; continue; }
#ifdef SENSOR_DHT22
      int c2 = line.indexOf(',', c1+1);
      String ts = "", t = "", h = "";
      if (c2 < 0) { t = line.substring(0, c1); h = line.substring(c1+1); }
      else { ts = line.substring(0, c1); t = line.substring(c1+1, c2); h = line.substring(c2+1); }
      String url = String(webAppUrl) + "?temperature=" + t + "&humidity=" + h
                 + "&board_id=" + urlEncode(boardID) + "&queued=1";
      if (ts.length() > 0 && ts != "0") url += "&timestamp=" + ts;
      url = appendGASAuth(url);
#else
      int c2 = line.indexOf(',', c1+1);
      String ts = line.substring(0, c1);
      String t  = (c2 < 0) ? line.substring(c1+1) : line.substring(c1+1, c2);
      String url = String(webAppUrl) + "?temperature=" + t
                 + "&board_id=" + urlEncode(boardID) + "&queued=1";
      if (ts.length() > 0 && ts != "0") url += "&timestamp=" + ts;
      url = appendGASAuth(url);
#endif
      HTTPClient http; bool ok = false;
      if (http.begin(client, url)) {
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setTimeout(10000);
        ESP.wdtDisable();
        int code = http.GET();
        String body = (code == 200) ? http.getString() : "";
        ESP.wdtEnable(8000);
        ok = (code == 200 && body.startsWith("OK"));
        if (!ok) Serial.println("Flush failed: HTTP " + String(code));
        http.end();
      }
      client.stop();
      if (ok) {
        sentCount++;
        display.fillRect(0, 40, 128, 20, BLACK);
        display.setCursor(5, 42); display.print("Sent: "); display.print(sentCount);
        display.print("/"); display.print(entryCount); display.display();
      } else break;
      ArduinoOTA.handle(); delay(500); yield();
    }
    f.close();
  }

  if (sentCount > 0) { lastSyncTimeEpoch = time(nullptr); lastSyncTimeMillis = millis(); }

  if (sentCount >= entryCount) { LittleFS.remove(QUEUE_FILE); Serial.println("Queue fully flushed!"); return; }

  // Pass 2: write unsent entries back
  {
    File src = LittleFS.open(QUEUE_FILE, "r");
    File dst = LittleFS.open("/qtmp.csv", "w");
    if (src && dst) {
      int lineNum = 0;
      while (src.available()) {
        ESP.wdtFeed();
        String line = src.readStringUntil('\n'); line.trim();
        if (line.length() <= 2) continue;
        if (lineNum++ < sentCount) continue;
        dst.println(line);
      }
    }
    src.close(); dst.close();
    LittleFS.remove(QUEUE_FILE); LittleFS.rename("/qtmp.csv", QUEUE_FILE);
    Serial.printf("Partial flush: %d/%d\n", sentCount, entryCount);
  }
}
