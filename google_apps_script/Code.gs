/**
 * TempBot - Google Apps Script
 * ============================================================
 * doGet  → รับข้อมูลจาก ESP8266 (HTTP GET) → บันทึกลง Sheet
 * doPost → รับ Webhook จาก LINE → ตอบคำถามจาก User
 *
 * ============================================================
 * วิธี Deploy:
 *   1. Extensions → Apps Script → วางโค้ดนี้
 *   2. Project Settings → Script Properties → เพิ่ม:
 *        KEY: LINE_TOKEN  VALUE: <Channel Access Token ของคุณ>
 *   3. Deploy → New deployment
 *      Type: Web app | Execute as: Me | Who: Anyone
 *   4. Copy Web App URL ไปตั้งเป็น Webhook URL ใน LINE Developers
 *      (Messaging API → Webhook URL → Verify)
 * ============================================================
 *
 * คำสั่ง LINE ที่รองรับ:
 *   temp / อุณหภูมิ / ล่าสุด  → ข้อมูลล่าสุด
 *   status / สถานะ             → สรุปทุกบอร์ด
 *   help / ช่วยเหลือ / คำสั่ง  → แสดงคำสั่งทั้งหมด
 * ============================================================
 */

// ============================================================
// CONFIG
// ============================================================
var TIMEZONE   = "Asia/Bangkok";
var SHEET_NAME = "";          // "" = Sheet แรก
var MAX_ROWS   = 10000;

var HEADERS = [
  "Timestamp",
  "Board ID",
  "Temperature (°C)",
  "Humidity (%)",
  "Data Type"
];

var LINE_REPLY_URL  = "https://api.line.me/v2/bot/message/reply";

// ============================================================
// doGet: รับข้อมูลจาก ESP8266
// ?temperature=28.5&humidity=65.2&board_id=BOARD_A1B2C3[&queued=1]
// ============================================================
function doGet(e) {
  try {
    var temperature = e.parameter.temperature;
    var humidity    = e.parameter.humidity;
    var boardId     = e.parameter.board_id;
    var isQueued    = (e.parameter.queued === "1");

    if (!temperature || !boardId) {
      return respond("ERROR: Missing temperature or board_id");
    }

    var tempVal  = parseFloat(temperature);
    var humidVal = parseFloat(humidity) || 0;

    if (isNaN(tempVal) || tempVal < -55 || tempVal > 125) {
      return respond("ERROR: Invalid temperature: " + temperature);
    }

    var sheet = getSheet();

    if (sheet.getLastRow() === 0) createHeader(sheet);

    var now       = new Date();
    var timestamp = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm:ss");

    sheet.appendRow([
      timestamp,
      boardId,
      tempVal,
      humidVal > 0 ? humidVal : "",
      isQueued ? "BUFFERED" : "LIVE"
    ]);

    formatLastRow(sheet, isQueued);
    if (MAX_ROWS > 0) trimOldRows(sheet);

    return respond("OK");

  } catch (err) {
    return respond("ERROR: " + err.toString());
  }
}

// ============================================================
// doPost: รับ Webhook จาก LINE Messaging API
// ============================================================
function doPost(e) {
  try {
    var body   = JSON.parse(e.postData.contents);
    var events = body.events;

    if (!events || events.length === 0) {
      return ContentService.createTextOutput("OK");
    }

    for (var i = 0; i < events.length; i++) {
      var event = events[i];
      // รองรับเฉพาะ text message
      if (event.type === "message" && event.message.type === "text") {
        handleTextMessage(event);
      }
    }

    return ContentService.createTextOutput("OK");

  } catch (err) {
    Logger.log("doPost error: " + err);
    return ContentService.createTextOutput("OK"); // ต้อง return 200 เสมอ
  }
}

// ============================================================
// handleTextMessage: จัดการคำสั่งจาก User
// ============================================================
function handleTextMessage(event) {
  var replyToken = event.replyToken;
  var rawText    = event.message.text;
  var text       = rawText.toLowerCase().trim();

  var response = "";

  if (["temp", "อุณหภูมิ", "ล่าสุด", "last", "now"].indexOf(text) !== -1) {
    // ข้อมูลล่าสุด (บอร์ดเดียว)
    response = getLatestEntry();

  } else if (["status", "สถานะ", "ทั้งหมด", "all"].indexOf(text) !== -1) {
    // สรุปทุกบอร์ด
    response = getAllBoardStatus();

  } else if (["help", "ช่วยเหลือ", "คำสั่ง", "?"].indexOf(text) !== -1) {
    response = "📋 TempBot คำสั่งที่ใช้ได้\n"
             + "─────────────────\n"
             + "• temp / อุณหภูมิ / ล่าสุด\n"
             + "  → ข้อมูลอุณหภูมิล่าสุด\n\n"
             + "• status / สถานะ / ทั้งหมด\n"
             + "  → สรุปทุกบอร์ด\n\n"
             + "• help / ช่วยเหลือ\n"
             + "  → แสดงคำสั่งนี้";
  }

  if (response !== "") {
    replyToLine(replyToken, response);
  }
}

// ============================================================
// getLatestEntry: ดึงข้อมูลแถวสุดท้ายจาก Sheet
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

  var msg = "🌡️ ข้อมูลล่าสุด\n";
  msg += "📟 " + boardId + "\n";
  msg += "🌡️ อุณหภูมิ: " + temp + "°C\n";
  if (humid !== "" && humid !== 0) {
    msg += "💧 ความชื้น: " + humid + "%\n";
  }
  msg += "🕐 " + timestamp;
  if (dataType === "BUFFERED") {
    msg += "\n⚠️ (ข้อมูลจาก Offline Buffer)";
  }

  return msg;
}

// ============================================================
// getAllBoardStatus: ข้อมูลล่าสุดแยกตามบอร์ด
// ============================================================
function getAllBoardStatus() {
  var sheet   = getSheet();
  var lastRow = sheet.getLastRow();

  if (lastRow <= 1) return "❌ ยังไม่มีข้อมูลในระบบ";

  // อ่านข้อมูลทั้งหมด (ไม่รวม header)
  var allData = sheet.getRange(2, 1, lastRow - 1, 5).getValues();

  // เก็บข้อมูลล่าสุดต่อบอร์ด (loop จากท้ายขึ้นหัว)
  var latestPerBoard = {};
  for (var i = allData.length - 1; i >= 0; i--) {
    var boardId = allData[i][1];
    if (boardId && !latestPerBoard[boardId]) {
      latestPerBoard[boardId] = allData[i];
    }
  }

  var boards = Object.keys(latestPerBoard);
  if (boards.length === 0) return "❌ ไม่พบข้อมูลบอร์ด";

  var msg = "📊 สถานะทุกบอร์ด (" + boards.length + " บอร์ด)\n";
  msg += "─────────────────\n";

  for (var b = 0; b < boards.length; b++) {
    var d = latestPerBoard[boards[b]];
    msg += "📟 " + d[1] + "\n";
    msg += "🌡️ " + d[2] + "°C";
    if (d[3] !== "" && d[3] !== 0) {
      msg += "  💧 " + d[3] + "%";
    }
    msg += "\n🕐 " + d[0];
    if (b < boards.length - 1) msg += "\n─────────────────\n";
  }

  return msg;
}

// ============================================================
// replyToLine: ส่ง reply กลับ LINE โดยใช้ replyToken
// ============================================================
function replyToLine(replyToken, text) {
  // ดึง token จาก Script Properties (ปลอดภัยกว่าเขียนใน code)
  var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");
  if (!token) {
    Logger.log("ERROR: LINE_TOKEN not found in Script Properties!");
    return;
  }

  var payload = JSON.stringify({
    replyToken: replyToken,
    messages: [{ type: "text", text: text }]
  });

  var options = {
    method      : "POST",
    contentType : "application/json",
    headers     : { "Authorization": "Bearer " + token },
    payload     : payload,
    muteHttpExceptions: true
  };

  try {
    var res = UrlFetchApp.fetch(LINE_REPLY_URL, options);
    Logger.log("LINE reply HTTP " + res.getResponseCode() + ": " + res.getContentText());
  } catch (err) {
    Logger.log("LINE reply error: " + err);
  }
}

// ============================================================
// Helper: ดึง Sheet
// ============================================================
function getSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  return SHEET_NAME
    ? (ss.getSheetByName(SHEET_NAME) || ss.getActiveSheet())
    : ss.getActiveSheet();
}

// ============================================================
// Helper: สร้าง Header row
// ============================================================
function createHeader(sheet) {
  sheet.appendRow(HEADERS);
  var r = sheet.getRange(1, 1, 1, HEADERS.length);
  r.setBackground("#1a73e8");
  r.setFontColor("#ffffff");
  r.setFontWeight("bold");
  r.setFontSize(11);
  sheet.setFrozenRows(1);
  sheet.setColumnWidth(1, 160);
  sheet.setColumnWidth(2, 140);
  sheet.setColumnWidth(3, 130);
  sheet.setColumnWidth(4, 110);
  sheet.setColumnWidth(5, 100);
}

// ============================================================
// Helper: จัดสีแถว
// ============================================================
function formatLastRow(sheet, isQueued) {
  var lastRow  = sheet.getLastRow();
  var rowRange = sheet.getRange(lastRow, 1, 1, HEADERS.length);
  rowRange.setBackground(
    isQueued ? "#fff3cd"
             : (lastRow % 2 === 0 ? "#f8f9fa" : "#ffffff")
  );
}

// ============================================================
// Helper: ลบ rows เก่าเกิน MAX_ROWS
// ============================================================
function trimOldRows(sheet) {
  var totalRows = sheet.getLastRow();
  if (totalRows > MAX_ROWS + 1) {
    sheet.deleteRows(2, totalRows - MAX_ROWS - 1);
  }
}

// ============================================================
// Helper: HTTP Response
// ============================================================
function respond(message) {
  return ContentService
    .createTextOutput(message)
    .setMimeType(ContentService.MimeType.TEXT);
}

// ============================================================
// TEST FUNCTIONS (รันใน Apps Script Editor)
// ============================================================
function testDoGet() {
  var result = doGet({ parameter: {
    temperature: "28.5", humidity: "65.2",
    board_id: "BOARD_TEST01", queued: "0"
  }});
  Logger.log(result.getContent());
}

function testLineReply_Temp() {
  handleTextMessage({
    replyToken : "test-reply-token",
    message    : { type: "text", text: "temp" }
  });
}

function testLineReply_Status() {
  handleTextMessage({
    replyToken : "test-reply-token",
    message    : { type: "text", text: "status" }
  });
}
