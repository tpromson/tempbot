/**
 * TempBot — Google Apps Script
 * ============================================================
 * Web App (doGet/doPost)  → รับข้อมูล ESP8266 + LINE Bot
 * Time‑Driven Triggers    → ตรวจ Sensor, รายงานอัตโนมัติ
 *
 * ── Script Properties (Extensions → Apps Script → Project Settings) ──
 *   LINE_TOKEN        = LINE Channel Access Token
 *   LINE_TARGET_ID    = LINE Group/Room/User ID (auto-saved เมื่อ bot รับข้อความแรก)
 *   IOTCENTER_API_URL = https://line-fleetbackend-production.up.railway.app
 *   IOTCENTER_API_KEY = (จาก IoTcenter dashboard)
 *   IOTCENTER_DEVICE  = ชื่อ device ใน IoTcenter
 *
 * ── Settings Sheet (สร้างอัตโนมัติครั้งแรก) ──
 *   คอลัมน์: Board ID | Max Temp (°C) | Min Temp (°C) | Bitmap | Updated
 *   - Board ID  : ต้องตรงกับ "Board Name" ที่กรอกใน ESP8266 config (case-sensitive)
 *                 ถ้าไม่กรอก board จะใช้ BOARD_XXXXXX (Chip ID)
 *   - Max Temp  : แจ้งเตือนเมื่ออุณหภูมิสูงกว่าค่านี้ (default 38.0)
 *   - Min Temp  : แจ้งเตือนเมื่ออุณหภูมิต่ำกว่าค่านี้ (default 20.0)
 *   - Bitmap    : animation บนจอ OLED — เลือก: cat / chicken / fish / tree (default tree)
 *                 บอร์ดจะดึงค่านี้ทุกครั้งที่ sync ข้อมูล (ทุก 10 นาที)
 *   - Updated   : timestamp อัปเดตล่าสุด (auto-filled)
 *
 * ── LINE Bot Commands ──
 *   temp                        → อุณหภูมิล่าสุด
 *   status                      → สรุปทุกบอร์ด
 *   สรุป [Board ID]             → รายงาน 24 ชม. + กราฟ
 *   ตั้ง max 35 [Board ID]      → ตั้งค่าแจ้งเตือนสูงสุด
 *   ตั้ง min 20 [Board ID]      → ตั้งค่าแจ้งเตือนต่ำสุด
 *   ตั้ง bitmap fish [Board ID] → เปลี่ยน animation (cat/chicken/fish/tree)
 *   ดูค่า [Board ID]            → ดูค่าตั้งทั้งหมด
 *   help                        → แสดงคำสั่งทั้งหมด
 *   (ถ้าไม่ระบุ Board ID จะใช้ค่า DEFAULT)
 *
 * ── Time-Driven Triggers (ตั้งใน Triggers) ──
 *   checkSensorStatus()   → ทุก 30 นาที — แจ้งเตือนถ้าบอร์ดขาดการติดต่อ > 35 นาที
 *   sendDailyReportPush() → วันละครั้ง — ส่งรายงาน 24 ชม. ทุกบอร์ด
 *   iotcenterHeartbeat()  → ทุก 15 นาที — ping IoTcenter
 *
 * ── การ Deploy ──
 *   1. เปิด Apps Script editor → คลิก "Deploy" → "New deployment"
 *   2. Type: "Web app"
 *   3. Execute as: "Me" (บัญชีเจ้าของ script)
 *   4. Who has access: "Anyone" (ESP8266 ไม่มี Google account)
 *   5. คลิก "Deploy" → copy Web App URL
 *   6. นำ URL ไปกรอกใน ESP8266 config field "Google WebApp URL"
 *
 *   ⚠️  ทุกครั้งที่แก้ code ต้อง Deploy ใหม่ ("Manage deployments" → "Edit" → version ใหม่)
 *       URL เดิมใช้ได้ตลอด ไม่ต้องอัปเดตบอร์ด
 *
 *   ⚠️  ถ้า script ผูกกับ Google Sheet:
 *       Extensions → Apps Script → Deploy จาก editor ของ Sheet นั้น
 *       แต่ละ Farm ใช้ Sheet แยกกัน → Deploy แยกกัน → URL แยกกัน
 */

// ============================================================
// IoTcenter Client
// ============================================================
var IoTcenter = (function() {
  'use strict';

  var _apiUrl = '';
  var _apiKey = '';
  var _deviceName = '';
  var _deviceType = '';

  function init(apiUrl, apiKey, deviceName, deviceType) {
    _apiUrl = apiUrl;
    _apiKey = apiKey;
    _deviceName = deviceName || '';
    _deviceType = deviceType || 'google_apps_script';
  }

  function _callApi(path, payload, retries) {
    retries = retries || 0;
    if (!_apiUrl || !_apiKey) {
      Logger.log('[IoTcenter] Not initialized. Skipping.');
      return null;
    }

    var options = {
      method: 'POST',
      headers: {
        'X-API-Key': _apiKey,
        'Content-Type': 'application/json'
      },
      payload: JSON.stringify(payload),
      muteHttpExceptions: true
    };

    try {
      var response = UrlFetchApp.fetch(_apiUrl + path, options);
      var status = response.getResponseCode();

      if (status === 201 || status === 200) {
        return JSON.parse(response.getContentText());
      }

      if ((status >= 500 || status === 429) && retries < 2) {
        Utilities.sleep(2000 * (retries + 1));
        return _callApi(path, payload, retries + 1);
      }

      Logger.log('[IoTcenter] Error ' + status + ': ' + response.getContentText());
      return null;
    } catch (e) {
      if (retries < 2) {
        Logger.log('[IoTcenter] Retry ' + (retries + 1) + '/2: ' + e.toString());
        Utilities.sleep(2000 * (retries + 1));
        return _callApi(path, payload, retries + 1);
      }
      Logger.log('[IoTcenter] Connection error: ' + e.toString());
      return null;
    }
  }

  function sendEvent(eventType, level, message, payload) {
    var data = {
      event_type: eventType,
      level: level || 'info',
      message: message || ''
    };
    if (payload) data.payload = payload;
    return _callApi('/api/iotcenter/events', data);
  }

  function sendHeartbeat(deviceName, deviceType, metadata) {
    var data = {
      device_name: deviceName || _deviceName,
      device_type: deviceType || _deviceType
    };
    if (metadata) data.metadata = metadata;
    return _callApi('/api/iotcenter/heartbeat', data);
  }

  return { init: init, sendEvent: sendEvent, sendHeartbeat: sendHeartbeat };
})();

// ============================================================
// IoTcenter Config
// ============================================================
function getIoTcenterConfig() {
  const props = PropertiesService.getScriptProperties();
  return {
    apiUrl: props.getProperty('IOTCENTER_API_URL') || 'https://line-fleetbackend-production.up.railway.app',
    apiKey: props.getProperty('IOTCENTER_API_KEY') || '',
    deviceName: props.getProperty('IOTCENTER_DEVICE') || 'TempBot'
  };
}

function sendToIoTcenter(boardId, eventType, level, message, payload) {
  var cfg = getIoTcenterConfig();
  if (!cfg.apiKey) return;
  IoTcenter.init(cfg.apiUrl, cfg.apiKey, boardId, 'iot');

  switch (eventType) {
    case 'heartbeat':
      IoTcenter.sendHeartbeat(boardId, 'iot', payload);
      break;
    default:
      IoTcenter.sendEvent(eventType, level, message, payload);
  }
}

// ============================================================
// CONFIG
// ============================================================
var TIMEZONE   = "Asia/Bangkok";
var SHEET_NAME = "";
var MAX_ROWS   = 10000;

var HEADERS = [
  "Timestamp",
  "Board ID",
  "Temperature (°C)",
  "Humidity (%)",
  "Data Type"
];

var LINE_REPLY_URL  = "https://api.line.me/v2/bot/message/reply";
var LINE_PUSH_URL   = "https://api.line.me/v2/bot/message/push";
var SETTINGS_SHEET  = "Settings";

var QUICK_REPLY_ITEMS = [
  { label: "🌡️ ล่าสุด", text: "temp" },
  { label: "⚙️ ตั้งค่า", text: "ดูค่า" },
  { label: "📈 สรุป", text: "สรุป" },
  { label: "❓ ช่วย", text: "help" }
];

var DEFAULT_THRESHOLDS = {
  maxTemp: 38.0,
  minTemp: 20.0
};

var DEFAULT_BITMAP = "tree";

// ============================================================
// ตั้งค่าเพิ่มเติมสำหรับ Time‑Driven Triggers
// ============================================================
// (เดิม hardcode TARGET_SPREADSHEET_ID ไว้ ทำให้ทุกบอร์ดอ่านชีตเดียวกัน — ลบออก
//  getTargetSheet() ใช้ชีตที่ผูกกับ project แทน เพื่อให้ Code.gs ตัวเดียว deploy ถูกทุกบอร์ด)
var TEMP_COLUMN = 3;
var TEMP_IDX = TEMP_COLUMN - 1;
var THRESHOLD = 30;
var MAX_PLAUSIBLE_TEMP = 35;
var MIN_PLAUSIBLE_TEMP = -10;

// ============================================================
// doGet — รับข้อมูลจาก ESP
// ============================================================
function doGet(e) {
  try {
    var temperature    = e.parameter.temperature;
    var humidity       = e.parameter.humidity;
    var boardId        = e.parameter.board_id;
    var isQueued       = (e.parameter.queued === "1");
    var timestampParam = e.parameter.timestamp;

    if (e.parameter.get_settings === "1") {
      if (!boardId || boardId.trim() === "") return respond("ERROR: Missing board_id");
      var thresholds = getThresholds(boardId.trim());
      return respond(JSON.stringify(thresholds));
    }

    // Board-side LINE notification routed through GAS (board no longer needs
    // the LINE token/group; GAS holds them in Script Properties)
    if (e.parameter.notify) {
      var np = PropertiesService.getScriptProperties();
      var target = np.getProperty("LINE_TARGET_ID") || np.getProperty("GROUP_ID");
      if (target) pushToLine(target, { type: "text", text: e.parameter.notify });
      var notifyMsg = e.parameter.notify;
      var notifyBoard = (boardId || "").trim();
      if (notifyMsg.indexOf("Watchdog") !== -1 || notifyMsg.indexOf("WDT") !== -1) {
        sendToIoTcenter(notifyBoard, 'BOOT_WDT', 'critical', notifyMsg, { boardId: notifyBoard });
      } else if (notifyMsg.indexOf("BOOT") !== -1) {
        sendToIoTcenter(notifyBoard, 'BOOT', 'info', notifyMsg, { boardId: notifyBoard });
      } else {
        sendToIoTcenter(notifyBoard, 'NOTIFY', 'warning', notifyMsg, { boardId: notifyBoard });
      }
      return respond("OK");
    }

    if (!temperature) return respond("ERROR: Missing temperature");
    if (!boardId || boardId.trim() === "") return respond("ERROR: Missing or empty board_id");

    var tempVal  = parseFloat(temperature);
    if (isNaN(tempVal) || tempVal < -55 || tempVal > 125) {
      return respond("ERROR: Invalid temperature: " + temperature);
    }

    var humidVal = parseFloat(humidity);
    if (humidity !== "" && humidity !== null && !isNaN(humidVal) && (humidVal < 0 || humidVal > 100)) {
      return respond("ERROR: Invalid humidity: " + humidity);
    }
    if (isNaN(humidVal) && humidity !== "" && humidity !== null) {
      humidVal = 0;
    } else if (isNaN(humidVal)) {
      humidVal = 0;
    }

    var sheet = getSheet();
    if (sheet.getLastRow() === 0) createHeader(sheet);

    var now = new Date();
    if (isQueued && timestampParam) {
      var epoch = parseInt(timestampParam, 10);
      if (!isNaN(epoch) && epoch > 0 && epoch > 1000000000 && epoch <= 9999999999) {
        now = new Date(epoch * 1000);
      }
    }

    var timestamp = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm:ss");
    var cleanBoardId = boardId.trim();

    sheet.appendRow([
      timestamp,
      cleanBoardId,
      tempVal,
      humidVal,
      isQueued ? "BUFFERED" : "LIVE"
    ]);

    formatLastRow(sheet, isQueued);
    if (MAX_ROWS > 0) trimOldRows(sheet);

    checkAndNotify(cleanBoardId, tempVal, humidVal);

    var iotPayload = { temperature: tempVal };
    if (humidVal > 0) iotPayload.humidity = humidVal;
    sendToIoTcenter(cleanBoardId, 'TEMP_NORMAL', 'info',
      tempVal.toFixed(1) + '°C' + (humidVal > 0 ? ' / ' + humidVal.toFixed(1) + '%' : ''),
      iotPayload
    );

    return respond("OK");

  } catch (err) {
    return respond("ERROR: " + err.toString());
  }
}

// ============================================================
// doPost — รับ Webhook จาก LINE
// ============================================================
function doPost(e) {
  try {
    var body   = JSON.parse(e.postData.contents);
    var events = body.events;
    if (!events || events.length === 0) return ContentService.createTextOutput("OK");

    for (var i = 0; i < events.length; i++) {
      var event = events[i];
      if (event.type === "message" && event.message.type === "text") {
        handleTextMessage(event);
      }
    }
    return ContentService.createTextOutput("OK");
  } catch (err) {
    Logger.log("doPost error: " + err);
    return ContentService.createTextOutput("OK");
  }
}

// ============================================================
// handleTextMessage — LINE Bot Commands
// ============================================================
function handleTextMessage(event) {
  var replyToken = event.replyToken;
  var rawText    = event.message.text;
  var text       = rawText.toLowerCase().trim();

  var source = event.source;
  var targetId = source.groupId || source.roomId || source.userId;
  if (targetId) {
    PropertiesService.getScriptProperties().setProperty("LINE_TARGET_ID", targetId);
  }

  if (["temp", "อุณหภูมิ", "ล่าสุด", "last", "now", "humid", "ความชื้น"].indexOf(text) !== -1) {
    replyToLine(replyToken, getLatestEntry());
  } else if (["status", "สถานะ", "ทั้งหมด", "all"].indexOf(text) !== -1) {
    replyToLine(replyToken, getAllBoardStatus());
  } else if (text.indexOf("สรุป") === 0 || text.indexOf("report") === 0 || text.indexOf("กราฟ") === 0) {
    var boardParam = "";
    var parts = rawText.trim().split(/\s+/);
    if (parts.length > 1) boardParam = parts.slice(1).join(" ");
    var report = generateDailyReport(boardParam);
    var messages = report.chartUrl
      ? [{ type: "text", text: report.text }, { type: "image", originalContentUrl: report.chartUrl, previewImageUrl: report.chartUrl }]
      : [{ type: "text", text: report.text }];
    replyToLine(replyToken, messages);
  } else if (text.indexOf("ตั้ง") === 0) {
    var parts = rawText.trim().split(/\s+/);
    if (parts.length >= 3) {
      var type = parts[1].toLowerCase();
      var strVal = parts[2].toLowerCase();
      var numVal = parseFloat(strVal);
      var targetBoard = parts.length >= 4 ? parts.slice(3).join(" ") : "DEFAULT";
      var VALID_BITMAPS = ["cat", "chicken", "fish", "tree"];
      if (type === "bitmap") {
        if (VALID_BITMAPS.indexOf(strVal) === -1) {
          replyToLine(replyToken, "❌ bitmap ไม่ถูกต้อง\nเลือกได้: " + VALID_BITMAPS.join(", "));
        } else {
          saveThreshold(targetBoard, null, null, strVal);
          replyToLine(replyToken, "✅ " + targetBoard + " bitmap: " + getThresholds(targetBoard).bitmap);
        }
      } else if (isNaN(numVal)) {
        replyToLine(replyToken, "❌ ค่าไม่ถูกต้อง\nลอง: ตั้ง max 35 / ตั้ง min 20 / ตั้ง bitmap cat");
      } else if (type === "max") {
        saveThreshold(targetBoard, numVal, null);
        replyToLine(replyToken, "✅ " + targetBoard + " MAX: " + getThresholds(targetBoard).maxTemp + " °C");
      } else if (type === "min") {
        saveThreshold(targetBoard, null, numVal);
        replyToLine(replyToken, "✅ " + targetBoard + " MIN: " + getThresholds(targetBoard).minTemp + " °C");
      } else {
        replyToLine(replyToken, "❌ ลอง: ตั้ง max 35 / ตั้ง min 20 / ตั้ง bitmap cat");
      }
    } else {
      replyToLine(replyToken, "❌ ข้อมูลไม่ครบ\nลอง: ตั้ง max 35 / ตั้ง min 20 / ตั้ง bitmap cat");
    }
  } else if (text === "ดูค่า" || text === "ตั้งค่า" || text === "ค่า") {
    var parts = rawText.trim().split(/\s+/);
    var targetBoard = parts.length >= 2 ? parts.slice(1).join(" ") : "DEFAULT";
    var t = getThresholds(targetBoard);
    replyToLine(replyToken, "📋 " + targetBoard + "\n─────\n🌡️ MAX: " + t.maxTemp + " °C\n🌡️ MIN: " + t.minTemp + " °C\n🖼️ Bitmap: " + t.bitmap + "\n─────\nเปลี่ยน: ตั้ง max 35\nตั้ง bitmap fish");
  } else if (["help", "ช่วยเหลือ", "คำสั่ง", "?"].indexOf(text) !== -1) {
    replyToLine(replyToken, "📋 TempBot\n─────\n• temp → ล่าสุด\n• status → สรุปทุกบอร์ด\n• สรุป → รายงาน 24 ชม. + กราฟ\n• ตั้ง max 35 → แจ้งเตือนสูงสุด\n• ตั้ง min 20 → แจ้งเตือนต่ำสุด\n• ตั้ง bitmap cat → เปลี่ยน animation\n• ดูค่า → ดูค่าตั้ง\n• help → คำสั่งนี้\n─────\nbitmaps: cat, chicken, fish, tree");
  }
}

// ============================================================
// getLatestEntry
// ============================================================
function getLatestEntry() {
  var sheet   = getSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow <= 1) return "❌ ยังไม่มีข้อมูลในระบบ";

  var row       = sheet.getRange(lastRow, 1, 1, 5).getValues()[0];
  var timestamp = row[0];
  var boardId   = row[1];
  var temp      = row[2];
  var humid     = row[3];
  var dataType  = row[4];

  var formattedTime = Utilities.formatDate(new Date(timestamp), TIMEZONE, "dd MMM. yy HH:mm") + " น.";
  var msg = "🌡️ ข้อมูลล่าสุด\n📟 " + boardId + "\n🌡️ " + temp + " °C\n";
  if (humid !== "" && humid !== null && humid !== undefined) msg += "💧 " + humid + " %\n";
  msg += "🕐 " + formattedTime;
  if (dataType === "BUFFERED") msg += "\n⚠️ (Offline Buffer)";
  return msg;
}

// ============================================================
// getAllBoardStatus
// ============================================================
function getAllBoardStatus() {
  var sheet   = getSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow <= 1) return "❌ ยังไม่มีข้อมูลในระบบ";

  var allData = sheet.getRange(2, 1, lastRow - 1, 5).getValues();
  var latestPerBoard = new Map();
  for (var i = allData.length - 1; i >= 0; i--) {
    var boardId = String(allData[i][1] || "");
    if (boardId && !latestPerBoard.has(boardId)) latestPerBoard.set(boardId, allData[i]);
  }

  if (latestPerBoard.size === 0) return "❌ ไม่พบข้อมูลบอร์ด";

  var msg = "📊 (" + latestPerBoard.size + " บอร์ด)\n─────\n";
  var boards = Array.from(latestPerBoard.keys());
  for (var b = 0; b < boards.length; b++) {
    var d = latestPerBoard.get(boards[b]);
    var boardTime = Utilities.formatDate(new Date(d[0]), TIMEZONE, "dd MMM. yy HH:mm") + " น.";
    msg += "📟 " + d[1] + "\n🌡️ " + d[2] + " °C";
    if (d[3] !== "" && d[3] !== 0) msg += "  💧 " + d[3] + " %";
    msg += "\n🕐 " + boardTime;
    if (b < boards.length - 1) msg += "\n─────\n";
  }
  return msg;
}

// ============================================================
// generateDailyReport — QuickChart กราฟ 24 ชม.
// ============================================================
function generateDailyReport(boardId) {
  var sheet = getSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow <= 1) return { text: "❌ ยังไม่มีข้อมูลในระบบ", chartUrl: null };

  var data = sheet.getRange(2, 1, lastRow - 1, 5).getValues();
  var now = new Date();
  var oneDayAgo = new Date(now.getTime() - 24 * 60 * 60 * 1000);

  var targetBoard = boardId ? boardId.trim() : "";
  if (targetBoard === "") {
    for (var i = data.length - 1; i >= 0; i--) {
      var rowDate = new Date(data[i][0]);
      if (rowDate >= oneDayAgo && data[i][1]) { targetBoard = String(data[i][1]); break; }
    }
  }
  if (targetBoard === "") return { text: "❌ ไม่พบข้อมูลบอร์ดใดๆ ใน 24 ชม.", chartUrl: null };

  var filtered = [];
  var targetLower = targetBoard.toLowerCase();
  for (var i = 0; i < data.length; i++) {
    var rowDate = new Date(data[i][0]);
    if (rowDate >= oneDayAgo && data[i][1] && String(data[i][1]).toLowerCase() === targetLower) {
      filtered.push({
        time: rowDate,
        temp: parseFloat(data[i][2]),
        humid: data[i][3] !== "" && data[i][3] !== undefined ? parseFloat(data[i][3]) : null
      });
    }
  }
  if (filtered.length === 0) return { text: "❌ ไม่พบข้อมูลบอร์ด \"" + targetBoard + "\" ใน 24 ชม.", chartUrl: null };

  var realBoardName = targetBoard;
  for (var i = data.length - 1; i >= 0; i--) {
    if (data[i][1] && String(data[i][1]).toLowerCase() === targetLower) { realBoardName = String(data[i][1]); break; }
  }

  var minTemp = 999, maxTemp = -999, sumTemp = 0, countTemp = 0;
  var minHumid = 999, maxHumid = -999, sumHumid = 0, countHumid = 0;
  for (var i = 0; i < filtered.length; i++) {
    var t = filtered[i].temp;
    var h = filtered[i].humid;
    if (t !== null && !isNaN(t) && t >= -55 && t <= 125) {
      if (t < minTemp) minTemp = t; if (t > maxTemp) maxTemp = t; sumTemp += t; countTemp++;
    }
    if (h !== null && !isNaN(h) && h >= 0 && h <= 100) {
      if (h < minHumid) minHumid = h; if (h > maxHumid) maxHumid = h; sumHumid += h; countHumid++;
    }
  }

  var avgTemp = countTemp > 0 ? (sumTemp / countTemp).toFixed(1) : "N/A";
  var avgHumid = countHumid > 0 ? (sumHumid / countHumid).toFixed(1) : "N/A";

  var minTempStr = minTemp !== 999 ? minTemp.toFixed(1) + "°C" : "N/A";
  var maxTempStr = maxTemp !== -999 ? maxTemp.toFixed(1) + "°C" : "N/A";
  var avgTempStr = avgTemp !== "N/A" ? avgTemp + "°C" : "N/A";

  var hasHumid = countHumid > 0;

  var buckets = [];
  for (var h = 23; h >= 0; h--) {
    var bucketTime = new Date(now.getTime() - h * 60 * 60 * 1000);
    buckets.push({
      label: Utilities.formatDate(bucketTime, TIMEZONE, "HH:00"),
      startTime: new Date(bucketTime.getFullYear(), bucketTime.getMonth(), bucketTime.getDate(), bucketTime.getHours(), 0, 0),
      endTime: new Date(bucketTime.getFullYear(), bucketTime.getMonth(), bucketTime.getDate(), bucketTime.getHours(), 59, 59),
      temps: [], humids: []
    });
  }

  for (var i = 0; i < filtered.length; i++) {
    var pt = filtered[i];
    var ptTimeMs = pt.time.getTime();
    for (var b = 0; b < buckets.length; b++) {
      if (ptTimeMs >= buckets[b].startTime.getTime() && ptTimeMs <= buckets[b].endTime.getTime() + 999) {
        if (pt.temp !== null && !isNaN(pt.temp)) buckets[b].temps.push(pt.temp);
        if (pt.humid !== null && !isNaN(pt.humid)) buckets[b].humids.push(pt.humid);
        break;
      }
    }
  }

  var labels = [], tempData = [], humidData = [];
  for (var b = 0; b < buckets.length; b++) {
    labels.push(buckets[b].label);
    tempData.push(buckets[b].temps.length > 0
      ? parseFloat((buckets[b].temps.reduce(function(x, y) { return x + y; }, 0) / buckets[b].temps.length).toFixed(1))
      : (tempData.length > 0 ? tempData[tempData.length - 1] : null));
    if (hasHumid) {
      humidData.push(buckets[b].humids.length > 0
        ? parseFloat((buckets[b].humids.reduce(function(x, y) { return x + y; }, 0) / buckets[b].humids.length).toFixed(1))
        : (humidData.length > 0 ? humidData[humidData.length - 1] : null));
    }
  }

  var chartConfig = {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: 'Temperature (°C)',
        borderColor: '#ff6384',
        backgroundColor: 'rgba(255, 99, 132, 0.08)',
        data: tempData, yAxisID: 'yTemp',
        fill: true, tension: 0.4, borderWidth: 3, pointRadius: 1.5
      }]
    },
    options: {
      title: { display: true, text: 'Daily: ' + realBoardName, fontSize: 14, fontStyle: 'bold' },
      legend: { display: true, position: 'bottom', labels: { fontSize: 10 } },
      scales: {
        xAxes: [{ gridLines: { display: false }, ticks: { fontSize: 8, maxTicksLimit: 12 } }],
        yAxes: [{
          id: 'yTemp', type: 'linear', position: 'left',
          scaleLabel: { display: true, labelString: 'Temperature (°C)', fontSize: 10 },
          ticks: { fontSize: 8 }
        }]
      }
    }
  };

  if (hasHumid) {
    chartConfig.data.datasets.push({
      label: 'Humidity (%)',
      borderColor: '#36a2eb',
      backgroundColor: 'rgba(54, 162, 235, 0.04)',
      data: humidData, yAxisID: 'yHumid',
      fill: true, tension: 0.4, borderWidth: 3, pointRadius: 1.5
    });
    chartConfig.options.scales.yAxes.push({
      id: 'yHumid', type: 'linear', position: 'right',
      scaleLabel: { display: true, labelString: 'Humidity (%)', fontSize: 10 },
      ticks: { min: 0, max: 100, fontSize: 8 },
      gridLines: { drawOnChartArea: false }
    });
  }

  var chartUrl = "";
  try {
    var shortenerRes = UrlFetchApp.fetch("https://quickchart.io/chart/create", {
      method: "POST", contentType: "application/json",
      payload: JSON.stringify({ width: 600, height: 380, backgroundColor: "white", chart: chartConfig }),
      muteHttpExceptions: true
    });
    if (shortenerRes.getResponseCode() === 200) {
      var resJson = JSON.parse(shortenerRes.getContentText());
      if (resJson.success) chartUrl = resJson.url;
    }
  } catch (e) {}

  if (!chartUrl) {
    chartUrl = "https://quickchart.io/chart?w=600&h=380&bkg=white&c=" + encodeURIComponent(JSON.stringify(chartConfig));
  }

  var msg = "📊 " + realBoardName + " — 24 ชม.\n──────\n🌡️ " + minTempStr + " - " + maxTempStr + " (เฉลี่ย " + avgTempStr + ")\n";
  if (hasHumid) msg += "💧 " + (minHumid !== 999 ? minHumid.toFixed(1) + "%" : "N/A") + " - " + (maxHumid !== -999 ? maxHumid.toFixed(1) + "%" : "N/A") + " (เฉลี่ย " + avgHumid + ")\n";
  msg += "📈 " + filtered.length + " ครั้ง\n──────";

  return { text: msg, chartUrl: chartUrl, boardId: realBoardName };
}

// ============================================================
// sendDailyReportPush — รายงานอัตโนมัติ 24 ชม.
// ============================================================
function sendDailyReportPush() {
  try {
    var targetId = PropertiesService.getScriptProperties().getProperty("LINE_TARGET_ID");
    var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");
    if (!targetId || !token) return;

    var sheet = getSheet();
    var lastRow = sheet.getLastRow();
    if (lastRow <= 1) return;

    var data = sheet.getRange(2, 1, lastRow - 1, 5).getValues();
    var now = new Date();
    var oneDayAgo = new Date(now.getTime() - 24 * 60 * 60 * 1000);

    var activeBoards = new Map();
    for (var i = 0; i < data.length; i++) {
      var rowDate = new Date(data[i][0]);
      if (rowDate >= oneDayAgo && data[i][1]) activeBoards.set(String(data[i][1]), true);
    }

    var boards = Array.from(activeBoards.keys());
    for (var b = 0; b < boards.length; b++) {
      var report = generateDailyReport(boards[b]);
      var messages = report.chartUrl
        ? [{ type: "text", text: report.text }, { type: "image", originalContentUrl: report.chartUrl, previewImageUrl: report.chartUrl }]
        : [{ type: "text", text: report.text }];

      pushToLine(targetId, messages);

      sendToIoTcenter(boards[b], 'DAILY_REPORT', 'info',
        'สรุป 24 ชม. ' + boards[b],
        { boardId: boards[b] }
      );

      Utilities.sleep(1500);
    }
  } catch (err) {
    Logger.log("sendDailyReportPush error: " + err.toString());
  }
}

// ============================================================
// pushToLine — LINE Push (หลายข้อความ)
// ============================================================
function pushToLine(targetId, messages, retries) {
  var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");
  if (!token) return;
  retries = retries || 0;
  var messageArray = Array.isArray(messages) ? messages : [messages];
  var options = {
    method: "POST", contentType: "application/json",
    headers: { "Authorization": "Bearer " + token },
    payload: JSON.stringify({ to: targetId, messages: messageArray }),
    muteHttpExceptions: true
  };
  try {
    var res = UrlFetchApp.fetch(LINE_PUSH_URL, options);
    if (res.getResponseCode() === 429 && retries < 2) { Utilities.sleep(1500 * (retries + 1)); pushToLine(targetId, messages, retries + 1); }
  } catch (err) {
    if (retries < 2) { Utilities.sleep(1500); pushToLine(targetId, messages, retries + 1); }
  }
}

// ============================================================
// replyToLine — LINE Reply (Quick Reply)
// ============================================================
function replyToLine(replyToken, messages, retries) {
  var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");
  if (!token) return;
  retries = retries || 0;
  var messageArray = Array.isArray(messages) ? messages : (typeof messages === "string" ? [{ type: "text", text: messages }] : [messages]);
  for (var i = 0; i < messageArray.length; i++) {
    if (messageArray[i].type === "text") {
      messageArray[i].quickReply = { items: QUICK_REPLY_ITEMS.map(function(item) {
        return { type: "action", action: { type: "message", label: item.label, text: item.text } };
      })};
    }
  }
  var options = {
    method: "POST", contentType: "application/json",
    headers: { "Authorization": "Bearer " + token },
    payload: JSON.stringify({ replyToken: replyToken, messages: messageArray }),
    muteHttpExceptions: true
  };
  try {
    var res = UrlFetchApp.fetch(LINE_REPLY_URL, options);
    if (res.getResponseCode() === 429 && retries < 2) { Utilities.sleep(1500 * (retries + 1)); replyToLine(replyToken, messages, retries + 1); }
  } catch (err) {
    if (retries < 2) { Utilities.sleep(1500); replyToLine(replyToken, messages, retries + 1); }
  }
}

// ============================================================
// pushMessage — ส่ง LINE ข้อความเดียว (สำหรับ Trigger)
// ============================================================
function pushMessage(text) {
  var config = getConfig();
  if (!config.token || !config.groupId) {
    Logger.log("pushMessage: Missing ACCESS_TOKEN or GROUP_ID");
    return;
  }
  pushToLine(config.groupId, [{ type: "text", text: text }]);
}

// ============================================================
// Helpers: Sheet, Settings, Thresholds
// ============================================================
function getSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  return SHEET_NAME ? (ss.getSheetByName(SHEET_NAME) || ss.getActiveSheet()) : ss.getActiveSheet();
}

function getTargetSheet() {
  return SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];
}

function getConfig() {
  const props = PropertiesService.getScriptProperties();
  return {
    token: props.getProperty('ACCESS_TOKEN'),
    groupId: props.getProperty('GROUP_ID')
  };
}

function getSettingsSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = ss.getSheetByName(SETTINGS_SHEET);
  if (!sheet) {
    sheet = ss.insertSheet(SETTINGS_SHEET);
    sheet.appendRow(["Board ID", "Max Temp (°C)", "Min Temp (°C)", "Bitmap", "Updated"]);
    sheet.getRange(1, 1, 1, 5).setBackground("#1a73e8").setFontColor("#ffffff").setFontWeight("bold");
    sheet.setColumnWidth(1, 150); sheet.setColumnWidth(2, 120);
    sheet.setColumnWidth(3, 120); sheet.setColumnWidth(4, 100); sheet.setColumnWidth(5, 160);
  }
  return sheet;
}

function getThresholds(boardId) {
  var sheet = getSettingsSheet();
  var data = sheet.getDataRange().getValues();
  var result = { maxTemp: DEFAULT_THRESHOLDS.maxTemp, minTemp: DEFAULT_THRESHOLDS.minTemp, maxHumid: 80.0, minHumid: 30.0, bitmap: DEFAULT_BITMAP };
  for (var i = 1; i < data.length; i++) {
    if (String(data[i][0] || "").trim() === boardId) {
      if (data[i][1] !== "") result.maxTemp = parseFloat(data[i][1]);
      if (data[i][2] !== "") result.minTemp = parseFloat(data[i][2]);
      if (data[i][3] !== "") result.bitmap = String(data[i][3]).trim();
      break;
    }
  }
  return result;
}

function saveThreshold(boardId, maxTemp, minTemp, bitmap) {
  var sheet = getSettingsSheet();
  var data = sheet.getDataRange().getValues();
  var now = new Date();
  var formattedTime = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm");
  var rowIndex = -1;
  var existingBitmap = DEFAULT_BITMAP;
  var existingMax = DEFAULT_THRESHOLDS.maxTemp;
  var existingMin = DEFAULT_THRESHOLDS.minTemp;
  for (var i = 1; i < data.length; i++) {
    if (String(data[i][0] || "").trim() === boardId) {
      rowIndex = i + 1;
      if (data[i][1] !== "") existingMax = parseFloat(data[i][1]);
      if (data[i][2] !== "") existingMin = parseFloat(data[i][2]);
      if (data[i][3] !== "") existingBitmap = String(data[i][3]).trim();
      break;
    }
  }
  var finalMax    = (maxTemp    !== null && maxTemp    !== undefined) ? maxTemp    : existingMax;
  var finalMin    = (minTemp    !== null && minTemp    !== undefined) ? minTemp    : existingMin;
  var finalBitmap = (bitmap     !== null && bitmap     !== undefined) ? bitmap     : existingBitmap;
  if (rowIndex > 0) {
    sheet.getRange(rowIndex, 2, 1, 4).setValues([[finalMax, finalMin, finalBitmap, formattedTime]]);
  } else {
    sheet.appendRow([boardId, finalMax, finalMin, finalBitmap, formattedTime]);
  }
}

function createHeader(sheet) {
  sheet.appendRow(HEADERS);
  var r = sheet.getRange(1, 1, 1, HEADERS.length);
  r.setBackground("#1a73e8").setFontColor("#ffffff").setFontWeight("bold").setFontSize(11);
  sheet.setFrozenRows(1);
  sheet.setColumnWidth(1, 160); sheet.setColumnWidth(2, 140);
  sheet.setColumnWidth(3, 130); sheet.setColumnWidth(4, 110); sheet.setColumnWidth(5, 100);
}

function formatLastRow(sheet, isQueued) {
  var lastRow = sheet.getLastRow();
  sheet.getRange(lastRow, 1, 1, HEADERS.length).setBackground(isQueued ? "#fff3cd" : (lastRow % 2 === 0 ? "#f8f9fa" : "#ffffff"));
}

function trimOldRows(sheet) {
  var totalRows = sheet.getLastRow();
  if (totalRows > (MAX_ROWS + 1)) {
    var rowsToDelete = totalRows - MAX_ROWS - 1;
    sheet.deleteRows(2, rowsToDelete);
  }
}

// ============================================================
// checkAndNotify — ตรวจ threshold + LINE + IoTcenter
// ============================================================
var ALERT_COOLDOWN_MS = 1800000;
var HYSTERESIS_TEMP = 0.5;
var HYSTERESIS_HUMID = 2.0;

function checkAndNotify(boardId, temp, humid) {
  var props = PropertiesService.getScriptProperties();
  var thresholds = getThresholds(boardId);

  var minT = thresholds.minTemp;
  var maxT = thresholds.maxTemp;
  var minH = thresholds.minHumid;
  var maxH = thresholds.maxHumid;

  var now = new Date().getTime();

  var lastStateKey = "LAST_ALERT_STATE_" + boardId;
  var lastTimeKey = "LAST_NOTIFY_TIME_" + boardId;
  var lastTempStateKey = "LAST_TEMP_STATE_" + boardId;
  var lastHumidStateKey = "LAST_HUMID_STATE_" + boardId;

  var lastState = props.getProperty(lastStateKey) || "NORMAL";
  var lastNotifyTime = parseInt(props.getProperty(lastTimeKey) || "0");
  var lastTempState = props.getProperty(lastTempStateKey) || "NORMAL";
  var lastHumidState = props.getProperty(lastHumidStateKey) || "NORMAL";

  var newTempState = "NORMAL";
  if (temp <= minT && temp > -50) {
    newTempState = "LOW";
  } else if (temp >= maxT) {
    newTempState = "HIGH";
  } else {
    if (lastTempState === "LOW" && temp < minT + HYSTERESIS_TEMP) newTempState = "LOW";
    else if (lastTempState === "HIGH" && temp > maxT - HYSTERESIS_TEMP) newTempState = "HIGH";
  }

  var newHumidState = "NORMAL";
  if (humid > 0) {
    if (humid <= minH) newHumidState = "LOW";
    else if (humid >= maxH) newHumidState = "HIGH";
    else {
      if (lastHumidState === "LOW" && humid < minH + HYSTERESIS_HUMID) newHumidState = "LOW";
      else if (lastHumidState === "HIGH" && humid > maxH - HYSTERESIS_HUMID) newHumidState = "HIGH";
    }
  }

  var messages = [];
  var newState = "NORMAL";

  if (newTempState !== "NORMAL") {
    messages.push("⚠️ " + (newTempState === "LOW" ? "อุณหภูมิต่ำ" : "อุณหภูมิสูง") + "\n🌡️ " + temp.toFixed(1) + " °C\n📟 " + boardId);
    newState = "ALERT";
  }

  if (newHumidState !== "NORMAL") {
    messages.push("⚠️ " + (newHumidState === "LOW" ? "ความชื้นต่ำ" : "ความชื้นสูง") + "\n💧 " + humid.toFixed(1) + " %\n📟 " + boardId);
    newState = "ALERT";
  }

  if (newTempState === "NORMAL" && newHumidState === "NORMAL" && (lastTempState !== "NORMAL" || lastHumidState !== "NORMAL")) {
    messages.push("✅ กลับมาปกติ\n🌡️ " + temp.toFixed(1) + " °C  |  💧 " + humid.toFixed(1) + " %\n📟 " + boardId);
    newState = "NORMAL";
  }

  var shouldSend = false;
  if (messages.length > 0) {
    if (newState !== lastState) shouldSend = true;
    else if (newState !== "NORMAL" && (now - lastNotifyTime) >= ALERT_COOLDOWN_MS) shouldSend = true;
  }

  if (shouldSend && messages.length > 0) {
    var targetId = props.getProperty("LINE_TARGET_ID");
    var token = props.getProperty("LINE_TOKEN");
    if (targetId && token) {
      for (var i = 0; i < messages.length; i++) {
        var opt = {
          method: "POST", contentType: "application/json",
          headers: { "Authorization": "Bearer " + token },
          payload: JSON.stringify({ to: targetId, messages: [{ type: "text", text: messages[i] }] }),
          muteHttpExceptions: true
        };
        try { UrlFetchApp.fetch(LINE_PUSH_URL, opt); } catch (e) {}
        Utilities.sleep(500);
      }
      props.setProperty(lastTimeKey, now.toString());
    }
  }

  if (shouldSend) {
    if (newTempState === 'HIGH') {
      sendToIoTcenter(boardId, 'HIGH_TEMP', 'warning',
        temp.toFixed(1) + '°C (max: ' + maxT + '°C)',
        { temperature: temp, threshold: maxT }
      );
    } else if (newTempState === 'LOW') {
      sendToIoTcenter(boardId, 'LOW_TEMP', 'warning',
        temp.toFixed(1) + '°C (min: ' + minT + '°C)',
        { temperature: temp, threshold: minT }
      );
    } else if (newState === 'NORMAL') {
      sendToIoTcenter(boardId, 'TEMP_RECOVERY', 'recovery',
        'ปกติ: ' + temp.toFixed(1) + '°C',
        { temperature: temp }
      );
    }
  }

  props.setProperty(lastStateKey, newState);
  props.setProperty(lastTempStateKey, newTempState);
  props.setProperty(lastHumidStateKey, newHumidState);
}

// ============================================================
// iotcenterHeartbeat — cron ทุก 15 นาที
// ============================================================
function iotcenterHeartbeat() {
  var iotCfg = getIoTcenterConfig();
  IoTcenter.init(iotCfg.apiUrl, iotCfg.apiKey, iotCfg.deviceName, iotCfg.deviceType);

  var sheet = getSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow <= 1) { IoTcenter.sendHeartbeat(); return; }

  var row = sheet.getRange(lastRow, 1, 1, 5).getValues()[0];
  var temp = parseFloat(row[2]);
  var payload = {};
  if (!isNaN(temp) && temp <= MAX_PLAUSIBLE_TEMP) payload.lastTemperature = temp;

  IoTcenter.sendHeartbeat(iotCfg.deviceName, iotCfg.deviceType, payload);
}

// ============================================================
// heartbeat — บอก IoTcenter ว่าทำงาน
// ============================================================
function heartbeat() {
  var iotCfg = getIoTcenterConfig();
  IoTcenter.init(iotCfg.apiUrl, iotCfg.apiKey, iotCfg.deviceName, iotCfg.deviceType);

  try {
    var sheet = getTargetSheet();
    var lastRow = sheet.getLastRow();
    var lastTemp = lastRow >= 2 ? sheet.getRange(lastRow, TEMP_COLUMN).getValue() : null;

    IoTcenter.sendHeartbeat(iotCfg.deviceName, iotCfg.deviceType, {
      lastTemperature: lastTemp,
      lastRow: lastRow
    });
  } catch (e) {
    IoTcenter.sendHeartbeat();
  }
}

// ============================================================
// checkSensorStatus — ตรวจ Sensor ขาดการติดต่อ
// ============================================================
function checkSensorStatus() {
  var iotCfg = getIoTcenterConfig();
  IoTcenter.init(iotCfg.apiUrl, iotCfg.apiKey, iotCfg.deviceName, iotCfg.deviceType);

  var sheet = getTargetSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow < 2) { IoTcenter.sendHeartbeat(); return; }

  var lastValue = sheet.getRange(lastRow, 1).getValue();
  var lastTemp = sheet.getRange(lastRow, TEMP_COLUMN).getValue();

  var props = PropertiesService.getScriptProperties();
  var lastStatus = props.getProperty("SENSOR_STATUS");

  var lastDate = parseDate(lastValue);

  if (lastDate && !isNaN(lastDate.getTime())) {
    var now = new Date();
    var diffInMinutes = (now.getTime() - lastDate.getTime()) / (1000 * 60);

    if (diffInMinutes > 35) {
      if (lastStatus !== "OFFLINE") {
        var lastTimeStr = Utilities.formatDate(lastDate, Session.getScriptTimeZone(), "HH:mm (dd/MM/yyyy)");
        pushMessage("🚨 ขาดการติดต่อจาก Sensor!\n─────────\n🌡️ ล่าสุด: " + lastTemp + " °C\n🕒 " + lastTimeStr + "\n📢 ตรวจสอบอุปกรณ์");

        props.setProperty("SENSOR_STATUS", "OFFLINE");

        IoTcenter.sendEvent('SENSOR_OFFLINE', 'critical',
          'Sensor ขาดการติดต่อ > ' + Math.round(diffInMinutes) + ' นาที',
          { lastTemperature: lastTemp, lastContact: lastDate.toISOString(), minutesSinceLastContact: Math.round(diffInMinutes) }
        );
      }
    } else {
      if (lastStatus === "OFFLINE") {
        pushMessage("✅ Sensor กลับมาปกติแล้ว!\n─────────\n🌡️ " + lastTemp + " °C\n⏰ " + Utilities.formatDate(lastDate, Session.getScriptTimeZone(), "HH:mm"));

        props.setProperty("SENSOR_STATUS", "OK");

        IoTcenter.sendEvent('SENSOR_RECOVERY', 'recovery',
          'Sensor กลับมาทำงานปกติ',
          { temperature: lastTemp }
        );
      }
    }
  }

  IoTcenter.sendHeartbeat();
}

// ============================================================
// checkTemperatureAlert — แจ้งเตือนอุณหภูมิเกิน
// ============================================================
function checkTemperatureAlert() {
  var iotCfg = getIoTcenterConfig();
  IoTcenter.init(iotCfg.apiUrl, iotCfg.apiKey, iotCfg.deviceName, iotCfg.deviceType);

  var sheet = getTargetSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow < 2) { IoTcenter.sendHeartbeat(); return; }

  var currentTemp = sheet.getRange(lastRow, TEMP_COLUMN).getValue();

  if (!isNaN(currentTemp) && currentTemp <= MAX_PLAUSIBLE_TEMP) {
    if (currentTemp >= THRESHOLD) {
      pushMessage("⚠️ อุณหภูมิสูงเกิน!\n─────────\n🌡️ " + currentTemp.toFixed(1) + " °C\n❗ เกณฑ์: " + THRESHOLD + " °C\n📢 ตรวจสอบ!");

      IoTcenter.sendEvent('HIGH_TEMP', 'warning',
        currentTemp.toFixed(1) + '°C (threshold: ' + THRESHOLD + '°C)',
        { temperature: currentTemp, threshold: THRESHOLD }
      );
    } else {
      IoTcenter.sendEvent('TEMP_NORMAL', 'info',
        currentTemp.toFixed(1) + '°C',
        { temperature: currentTemp }
      );
    }
  }

  IoTcenter.sendHeartbeat();
}

// ============================================================
// sendDailySummary — สรุปประจำวัน
// ============================================================
function sendDailySummary() {
  var iotCfg = getIoTcenterConfig();
  IoTcenter.init(iotCfg.apiUrl, iotCfg.apiKey, iotCfg.deviceName, iotCfg.deviceType);

  var sheet = getTargetSheet();
  var data = sheet.getDataRange().getValues();

  var yesterday = new Date();
  yesterday.setDate(yesterday.getDate() - 1);
  var targetDateStr = Utilities.formatDate(yesterday, Session.getScriptTimeZone(), "dd/MM/yyyy");

  var validCount = 0;
  var minTemp = Infinity;
  var maxTemp = -Infinity;
  var sumTemp = 0;

  for (var i = data.length - 1; i >= 1; i--) {
    var valDate = data[i][0];
    if (!valDate) continue;

    var rowDate = parseDate(valDate);
    if (isNaN(rowDate.getTime())) continue;

    var rowDateStr = Utilities.formatDate(rowDate, Session.getScriptTimeZone(), "dd/MM/yyyy");

    if (rowDateStr === targetDateStr) {
      var temp = parseFloat(data[i][TEMP_IDX]);
      if (!isNaN(temp) && temp <= MAX_PLAUSIBLE_TEMP && temp >= MIN_PLAUSIBLE_TEMP) {
        if (temp < minTemp) minTemp = temp;
        if (temp > maxTemp) maxTemp = temp;
        sumTemp += temp;
        validCount++;
      }
    } else if (validCount > 0) {
      break;
    }
  }

  if (validCount > 0) {
    var avgTemp = sumTemp / validCount;
    pushMessage("📊 สรุปอุณหภูมิ " + targetDateStr + "\n─────────\n🌡️ สูงสุด: " + maxTemp.toFixed(1) + " °C\n❄️ ต่ำสุด: " + minTemp.toFixed(1) + " °C\n📈 เฉลี่ย: " + avgTemp.toFixed(1) + " °C");

    IoTcenter.sendEvent('DAILY_REPORT', 'info',
      'สรุป ' + targetDateStr + ' | สูงสุด ' + maxTemp.toFixed(1) + '°C / ต่ำสุด ' + minTemp.toFixed(1) + '°C / เฉลี่ย ' + avgTemp.toFixed(1) + '°C',
      { date: targetDateStr, maxTemp: maxTemp, minTemp: minTemp, avgTemp: avgTemp, recordsCount: validCount }
    );
  } else {
    pushMessage("⚠️ ไม่พบข้อมูลวันที่ " + targetDateStr);
    IoTcenter.sendEvent('DAILY_REPORT_EMPTY', 'warning', 'ไม่พบข้อมูลวันที่ ' + targetDateStr, { date: targetDateStr });
  }

  IoTcenter.sendHeartbeat();
}

// ============================================================
// Shift Reports — ทุก 8 ชั่วโมง
// ============================================================
function sendReport_00_08() { generateReport(0, 8, "00.00-08.00", 0); }
function sendReport_08_16() { generateReport(8, 16, "08.00-16.00", 0); }
function sendReport_16_00() { generateReport(16, 24, "16.00-24.00", -1); }

function generateReport(startHour, endHour, periodName, daysOffset) {
  var iotCfg = getIoTcenterConfig();
  IoTcenter.init(iotCfg.apiUrl, iotCfg.apiKey, iotCfg.deviceName, iotCfg.deviceType);

  var sheet = getTargetSheet();
  var data = sheet.getDataRange().getValues();

  var targetDate = new Date();
  targetDate.setDate(targetDate.getDate() + daysOffset);

  var targetD = targetDate.getDate();
  var targetM = targetDate.getMonth();
  var targetY = targetDate.getFullYear();

  var minTemp = Infinity;
  var maxTemp = -Infinity;
  var sumTemp = 0;
  var count = 0;
  var hasData = false;

  for (var i = 1; i < data.length; i++) {
    var dateObj = parseDate(data[i][0]);
    if (isNaN(dateObj.getTime())) continue;

    if (dateObj.getDate() === targetD && dateObj.getMonth() === targetM && dateObj.getFullYear() === targetY) {
      var rowHour = dateObj.getHours();
      if (rowHour >= startHour && rowHour < endHour) {
        var temp = parseFloat(data[i][TEMP_IDX]);
        if (!isNaN(temp) && temp <= MAX_PLAUSIBLE_TEMP && temp >= MIN_PLAUSIBLE_TEMP) {
          if (temp < minTemp) minTemp = temp;
          if (temp > maxTemp) maxTemp = temp;
          sumTemp += temp;
          count++;
          hasData = true;
        }
      }
    }
  }

  var dateString = Utilities.formatDate(targetDate, Session.getScriptTimeZone(), "dd/MM/yyyy");

  if (hasData) {
    var avgTemp = sumTemp / count;
    pushMessage("📊 " + periodName + " — " + dateString + "\n─────────\n🌡️ สูงสุด: " + maxTemp.toFixed(1) + " °C\n❄️ ต่ำสุด: " + minTemp.toFixed(1) + " °C\n📈 เฉลี่ย: " + avgTemp.toFixed(1) + " °C");

    IoTcenter.sendEvent('SHIFT_REPORT', 'info',
      periodName + ' — ' + dateString + ' | สูงสุด ' + maxTemp.toFixed(1) + '°C / ต่ำสุด ' + minTemp.toFixed(1) + '°C / เฉลี่ย ' + avgTemp.toFixed(1) + '°C',
      { period: periodName, date: dateString, maxTemp: maxTemp, minTemp: minTemp, avgTemp: avgTemp, recordsCount: count }
    );
  } else {
    pushMessage("⚠️ ไม่พบข้อมูลช่วง " + periodName + " — " + dateString);
    IoTcenter.sendEvent('SHIFT_REPORT_EMPTY', 'warning', 'ไม่พบข้อมูลช่วง ' + periodName, { period: periodName });
  }

  IoTcenter.sendHeartbeat();
}

// ============================================================
// parseDate — แปลงวันที่หลายรูปแบบ
// ============================================================
function parseDate(valDate) {
  if (valDate instanceof Date) return valDate;
  var cleanStr = valDate.toString().replace(/-/g, "/").trim();
  var dateObj = new Date(cleanStr);

  if (isNaN(dateObj.getTime())) {
    var m = cleanStr.match(/(\d{1,2})\/(\d{1,2})\/(\d{4})/);
    if (m) {
      dateObj = new Date(m[3], m[2]-1, m[1]);
      var hMatch = cleanStr.match(/\s(\d{1,2}):(\d{1,2})/);
      if (hMatch) {
        dateObj.setHours(hMatch[1]);
        dateObj.setMinutes(hMatch[2]);
      }
    }
  }
  return dateObj;
}

// ============================================================
// respond
// ============================================================
function respond(text) {
  return ContentService.createTextOutput(text).setMimeType(ContentService.MimeType.TEXT);
}

// ============================================================
// onOpen
// ============================================================
function onOpen() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = ss.getActiveSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow > 0) {
    var lastRange = sheet.getRange(lastRow, 1);
    sheet.setActiveRange(lastRange);
    ss.setActiveSelection(lastRange);
  }
}
