/**
 * TempBot + IoTcenter Integration
 * ============================================================
 * doGet  → รับข้อมูลจาก ESP8266 → บันทึกลง Sheet + ส่ง IoTcenter
 * doPost → รับ Webhook จาก LINE → ตอบคำถามจาก User
 *
 * Script Properties (File → Project Properties):
 *   LINE_TOKEN            = <Channel Access Token>
 *   IOTCENTER_API_URL     = https://line-fleetbackend-production.up.railway.app
 *   IOTCENTER_API_KEY     = (จาก IoTcenter Setup → Sources → API Key)
 *   IOTCENTER_DEVICE      = BOARD_A1B2C3 (ชื่อเดียวกับ board_id)
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
    _deviceType = deviceType || 'iot';
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

      if (status === 201 || status === 200) return JSON.parse(response.getContentText());

      if ((status >= 500 || status === 429) && retries < 2) {
        Utilities.sleep(2000 * (retries + 1));
        return _callApi(path, payload, retries + 1);
      }

      Logger.log('[IoTcenter] Error ' + status + ': ' + response.getContentText());
      return null;
    } catch (e) {
      if (retries < 2) {
        Logger.log('[IoTcenter] Retry ' + (retries + 1) + '/2 after: ' + e.toString());
        Utilities.sleep(2000 * (retries + 1));
        return _callApi(path, payload, retries + 1);
      }
      Logger.log('[IoTcenter] Connection error after retries: ' + e.toString());
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
    return _callApi('/api/iotcenter/heartbeat', {
      device_name: deviceName || _deviceName,
      device_type: deviceType || _deviceType,
      metadata: metadata || {}
    });
  }

  return { init: init, sendEvent: sendEvent, sendHeartbeat: sendHeartbeat };
})();

// ============================================================
// IoTcenter Config — ตั้งค่าใน Script Properties
// ============================================================
function getIoTcenterConfig() {
  var props = PropertiesService.getScriptProperties();
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
// iotcenterHeartbeat — cron ทุก 15 นาที
// ============================================================
function iotcenterHeartbeat() {
  var sheet = getSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow <= 1) return;

  var row = sheet.getRange(lastRow, 1, 1, 5).getValues()[0];
  var boardId = String(row[1] || '');
  if (!boardId) return;

  var temp = parseFloat(row[2]);
  var humid = parseFloat(row[3]) || 0;

  var payload = {};
  if (!isNaN(temp)) payload.lastTemperature = temp;
  if (humid > 0) payload.lastHumidity = humid;

  sendToIoTcenter(boardId, 'heartbeat', 'info', 'Sensor active', payload);
}

// ============================================================
// CONFIG — ESP8266 TempBot Settings
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
// doGet: รับข้อมูลจาก ESP8266
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

    // ✅ IoTcenter: ส่ง temperature reading
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
// doPost: รับ Webhook จาก LINE
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
// handleTextMessage: จัดการคำสั่งจาก User
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
      var value = parseFloat(parts[2]);
      var targetBoard = parts.length >= 4 ? parts.slice(3).join(" ") : "DEFAULT";
      if (isNaN(value)) {
        replyToLine(replyToken, "❌ ค่าไม่ถูกต้อง ลองใหม่ เช่น 'ตั้ง max 35'");
      } else if (type === "max") {
        saveThreshold(targetBoard, value, null);
        replyToLine(replyToken, "✅ ตั้ง MAX " + targetBoard + ": " + getThresholds(targetBoard).maxTemp + " °C");
      } else if (type === "min") {
        saveThreshold(targetBoard, null, value);
        replyToLine(replyToken, "✅ ตั้ง MIN " + targetBoard + ": " + getThresholds(targetBoard).minTemp + " °C");
      } else {
        replyToLine(replyToken, "❌ ไม่รู้จัก ลอง 'ตั้ง max 35' หรือ 'ตั้ง min 20'");
      }
    } else {
      replyToLine(replyToken, "❌ ข้อมูลไม่ครบ\nลอง: ตั้ง max 35 หรือ ตั้ง min 20");
    }
  } else if (text === "ดูค่า" || text === "ตั้งค่า" || text === "ค่า") {
    var parts = rawText.trim().split(/\s+/);
    var targetBoard = parts.length >= 2 ? parts.slice(1).join(" ") : "DEFAULT";
    var t = getThresholds(targetBoard);
    replyToLine(replyToken, "📋 ค่าตั้งของ " + targetBoard + "\n─────────────────\n🌡️ MAX: " + t.maxTemp + " °C\n🌡️ MIN: " + t.minTemp + " °C\n─────────────────\nเปลี่ยน: ตั้ง max 35 หรือ ตั้ง min 20");
  } else if (["help", "ช่วยเหลือ", "คำสั่ง", "?"].indexOf(text) !== -1) {
    replyToLine(replyToken, "📋 TempBot คำสั่งที่ใช้ได้\n─────────────────\n• temp / อุณหภูมิ / ความชื้น / ล่าสุด → ข้อมูลล่าสุด\n• status / สถานะ / ทั้งหมด → สรุปทุกบอร์ด\n• สรุป / report / กราฟ → รายงาน 24 ชม. + กราฟ\n• ตั้ง max 35 / ตั้ง min 20 → ตั้งค่าแจ้งเตือน\n• ดูค่า → ดูค่าตั้งปัจจุบัน\n• help / ช่วยเหลือ → แสดงคำสั่งนี้");
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
  var msg = "🌡️ ข้อมูลล่าสุด\n📟 " + boardId + "\n🌡️ อุณหภูมิ: " + temp + " °C\n";
  if (humid !== "" && humid !== null && humid !== undefined) msg += "💧 ความชื้น: " + humid + " %\n";
  msg += "🕐 " + formattedTime;
  if (dataType === "BUFFERED") msg += "\n⚠️ (ข้อมูลจาก Offline Buffer)";
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

  var msg = "📊 สถานะทุกบอร์ด (" + latestPerBoard.size + " บอร์ด)\n─────────────────\n";
  var boards = Array.from(latestPerBoard.keys());
  for (var b = 0; b < boards.length; b++) {
    var d = latestPerBoard.get(boards[b]);
    var boardTime = Utilities.formatDate(new Date(d[0]), TIMEZONE, "dd MMM. yy HH:mm") + " น.";
    msg += "📟 " + d[1] + "\n🌡️ " + d[2] + " °C";
    if (d[3] !== "" && d[3] !== 0) msg += "  💧 " + d[3] + " %";
    msg += "\n🕐 " + boardTime;
    if (b < boards.length - 1) msg += "\n─────────────────\n";
  }
  return msg;
}

// ============================================================
// generateDailyReport
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
  if (targetBoard === "") return { text: "❌ ไม่พบข้อมูลบอร์ดใดๆ ในช่วง 24 ชั่วโมงที่ผ่านมา", chartUrl: null };

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
  if (filtered.length === 0) return { text: "❌ ไม่พบข้อมูลสำหรับบอร์ด \"" + targetBoard + "\" ในช่วง 24 ชั่วโมงที่ผ่านมา", chartUrl: null };

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

  var minHumidStr = minHumid !== 999 ? minHumid.toFixed(1) + "%" : "N/A";
  var maxHumidStr = maxHumid !== -999 ? maxHumid.toFixed(1) + "%" : "N/A";
  var avgHumidStr = avgHumid !== "N/A" ? avgHumid + "%" : "N/A";

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
    humidData.push(buckets[b].humids.length > 0
      ? parseFloat((buckets[b].humids.reduce(function(x, y) { return x + y; }, 0) / buckets[b].humids.length).toFixed(1))
      : (humidData.length > 0 ? humidData[humidData.length - 1] : null));
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
      title: { display: true, text: 'Daily Report: ' + realBoardName + ' (Last 24 Hours)', fontSize: 14, fontStyle: 'bold' },
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

  var msg = "📊 " + realBoardName + " - สรุป 24 ชม.\n──────────────────\n🌡️ อุณหภูมิ: " + minTempStr + " - " + maxTempStr + " (เฉลี่ย " + avgTempStr + ")\n";
  if (hasHumid) msg += "💧 ความชื้น: " + minHumidStr + " - " + maxHumidStr + " (เฉลี่ย " + avgHumidStr + ")\n";
  msg += "📈 บันทึก " + filtered.length + " ครั้ง\n──────────────────";

  return { text: msg, chartUrl: chartUrl, boardId: realBoardName };
}

// ============================================================
// sendDailyReportPush — ส่งรายงานอัตโนมัติ
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

      // ✅ IoTcenter: ส่ง DAILY_REPORT
      sendToIoTcenter(boards[b], 'DAILY_REPORT', 'info',
        'สรุป 24 ชม. ' + boards[b],
        { boardId: boards[b], records: report.boardId ? 0 : 0 }
      );

      Utilities.sleep(1500);
    }
  } catch (err) {
    Logger.log("sendDailyReportPush error: " + err.toString());
  }
}

// ============================================================
// pushToLine
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
// replyToLine
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
// Helpers: Sheet, Settings, Thresholds
// ============================================================
function getSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  return SHEET_NAME ? (ss.getSheetByName(SHEET_NAME) || ss.getActiveSheet()) : ss.getActiveSheet();
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

function saveThreshold(boardId, maxTemp, minTemp) {
  var sheet = getSettingsSheet();
  var data = sheet.getDataRange().getValues();
  var now = new Date();
  var formattedTime = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm");
  var rowIndex = -1;
  for (var i = 1; i < data.length; i++) {
    if (String(data[i][0] || "").trim() === boardId) { rowIndex = i + 1; break; }
  }
  if (rowIndex > 0) {
    sheet.getRange(rowIndex, 2, 1, 3).setValues([[maxTemp, minTemp, formattedTime]]);
  } else {
    sheet.appendRow([boardId, maxTemp, minTemp, formattedTime]);
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
// checkAndNotify — ตรวจสอบ threshold + LINE notify
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
    if (newTempState === "LOW") {
      messages.push("⚠️ อุณหภูมิต่ำกว่าค่าตั้ง\n🌡️ " + temp.toFixed(1) + " °C\n📉 ต่ำสุด: " + minT + " °C\n📟 " + boardId);
    } else {
      messages.push("⚠️ อุณหภูมิสูงกว่าค่าตั้ง\n🌡️ " + temp.toFixed(1) + " °C\n📈 สูงสุด: " + maxT + " °C\n📟 " + boardId);
    }
    newState = "ALERT";
  }

  if (newHumidState !== "NORMAL") {
    if (newHumidState === "LOW") {
      messages.push("⚠️ ความชื้นต่ำกว่าค่าตั้ง\n💧 " + humid.toFixed(1) + " %\n📉 ต่ำสุด: " + minH + " %\n📟 " + boardId);
    } else {
      messages.push("⚠️ ความชื้นสูงกว่าค่าตั้ง\n💧 " + humid.toFixed(1) + " %\n📈 สูงสุด: " + maxH + " %\n📟 " + boardId);
    }
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
      for (var i = 0; i < messages.length; i++) { pushNotifyToLine(targetId, messages[i], token); Utilities.sleep(500); }
      props.setProperty(lastTimeKey, now.toString());
    }
  }

  // ✅ IoTcenter: ส่ง alert/recovery events
  if (shouldSend) {
    if (newTempState === 'HIGH') {
      sendToIoTcenter(boardId, 'HIGH_TEMP', 'warning',
        'อุณหภูมิสูงเกิน: ' + temp.toFixed(1) + '°C (max: ' + maxT + '°C)',
        { temperature: temp, threshold: maxT, maxTemp: maxT }
      );
    } else if (newTempState === 'LOW') {
      sendToIoTcenter(boardId, 'LOW_TEMP', 'warning',
        'อุณหภูมิต่ำเกิน: ' + temp.toFixed(1) + '°C (min: ' + minT + '°C)',
        { temperature: temp, threshold: minT, minTemp: minT }
      );
    } else if (newState === 'NORMAL') {
      sendToIoTcenter(boardId, 'TEMP_RECOVERY', 'recovery',
        'อุณหภูมิกลับมาปกติ: ' + temp.toFixed(1) + '°C',
        { temperature: temp }
      );
    }
  }

  props.setProperty(lastStateKey, newState);
  props.setProperty(lastTempStateKey, newTempState);
  props.setProperty(lastHumidStateKey, newHumidState);
}

function pushNotifyToLine(targetId, message, token) {
  var options = {
    method: "POST", contentType: "application/json",
    headers: { "Authorization": "Bearer " + token },
    payload: JSON.stringify({ to: targetId, messages: [{ type: "text", text: message }] }),
    muteHttpExceptions: true
  };
  try { UrlFetchApp.fetch(LINE_PUSH_URL, options); } catch (e) { Logger.log("LINE Notify error: " + e.toString()); }
}

// ============================================================
// respond: ContentService helper
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
