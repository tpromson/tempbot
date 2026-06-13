#include "tempbot_common.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#endif
#include <WiFiClientSecure.h>

// ===== formatTime =====
String formatTime(time_t epoch, bool includeSeconds) {
  if (epoch < 1000000000) {
    return "--:--";
  }
  struct tm* timeinfo = localtime(&epoch);
  char buffer[10];
  if (includeSeconds) {
    sprintf(buffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  } else {
    sprintf(buffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  }
  return String(buffer);
}

// ===== urlEncode =====
String urlEncode(String str) {
  String encoded = "";
  char buf[4];
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// ===== getBoardIdentifier =====
String getBoardIdentifier() {
  String bName = String(boardName);
  bName.trim();
  if (bName.length() == 0) {
#if defined(ESP8266)
    bName = "BOARD_" + String(ESP.getChipId(), HEX);
#elif defined(ESP32)
    bName = "BOARD_" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);
#endif
    bName.toUpperCase();
  }
  return bName;
}

// ===== sendLineNotify =====
void sendLineNotify(String message) {
  String tokenStr = String(lineToken);
  tokenStr.trim();
  String groupIdStr = String(lineGroupId);
  groupIdStr.trim();

  if (tokenStr.length() == 0 || groupIdStr.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (http.begin(client, "https://api.line.me/v2/bot/message/push")) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + tokenStr);

    String safeMsg = message;
    safeMsg.replace("\\", "\\\\");
    safeMsg.replace("\"", "\\\"");
    safeMsg.replace("\n", "\\n");
    safeMsg.replace("\r", "\\r");
    String body = "{\"to\":\"" + groupIdStr + "\","
                  "\"messages\":[{\"type\":\"text\",\"text\":\"" + safeMsg + "\"}]}";

    Serial.print("LINE API Token Len: "); Serial.println(tokenStr.length());
    Serial.print("LINE API Group ID:  "); Serial.println(groupIdStr);
    Serial.print("LINE API Payload:   "); Serial.println(body);

    int httpCode = http.POST(body);
    if (httpCode == 200) {
      Serial.println("LINE API: Message sent successfully.");
    } else {
      Serial.print("LINE API: Failed, code "); Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("LINE API: Failed to connect.");
  }
}

// ===== getTempCalibrationOffset =====
float getTempCalibrationOffset() {
  return atof(tempCalibrationStr);
}

// ===== isNewerVersion =====
static void parseVer(String v, int &a, int &b, int &c) {
  v.trim();
  a = b = c = 0;
  int p1 = v.indexOf('.');
  if (p1 < 0) { a = v.toInt(); return; }
  a = v.substring(0, p1).toInt();
  int p2 = v.indexOf('.', p1 + 1);
  if (p2 < 0) { b = v.substring(p1 + 1).toInt(); return; }
  b = v.substring(p1 + 1, p2).toInt();
  c = v.substring(p2 + 1).toInt();
}

bool isNewerVersion(String latest, String current) {
  int la, lb, lc, ca, cb, cc;
  parseVer(latest, la, lb, lc);
  parseVer(current, ca, cb, cc);
  if (la != ca) return la > ca;
  if (lb != cb) return lb > cb;
  return lc > cc;
}
