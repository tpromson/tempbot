/**
 * TempBot - Google Apps Script
 * ============================================================
 * รับข้อมูลอุณหภูมิ/ความชื้นจาก ESP8266 ผ่าน HTTP GET
 * แล้วบันทึกลง Google Sheets พร้อมรองรับ Offline Buffer
 *
 * วิธี Deploy:
 *   1. Extensions → Apps Script → วางโค้ดนี้
 *   2. Deploy → New deployment
 *   3. Type: Web app
 *   4. Execute as: Me
 *   5. Who has access: Anyone  ← สำคัญมาก!
 *   6. Copy Web App URL ไปใส่ใน ESP8266 Config Portal
 *
 * Parameters ที่รับจาก ESP8266:
 *   ?temperature=28.5&humidity=65.2&board_id=BOARD_A1B2C3[&queued=1]
 *   - temperature : อุณหภูมิ (°C)
 *   - humidity    : ความชื้น (%) — ส่ง 0 สำหรับ DS18B20
 *   - board_id    : รหัสบอร์ด เช่น BOARD_A1B2C3
 *   - queued      : (optional) "1" = ข้อมูลจาก Offline Buffer
 * ============================================================
 */

// ============================================================
// CONFIG: ชื่อ Sheet และ Timezone
// ============================================================
var TIMEZONE      = "Asia/Bangkok";   // เขตเวลา (UTC+7)
var SHEET_NAME    = "";               // "" = ใช้ Sheet แรก, หรือใส่ชื่อ เช่น "Data"
var MAX_ROWS      = 10000;            // ลบ rows เก่าเมื่อเกิน (0 = ไม่ลบ)

// ============================================================
// HEADER: คอลัมน์บน Spreadsheet
// ============================================================
var HEADERS = [
  "Timestamp",
  "Board ID",
  "Temperature (°C)",
  "Humidity (%)",
  "Data Type"       // LIVE หรือ BUFFERED
];

// ============================================================
// doGet: รับข้อมูลจาก ESP8266
// ============================================================
function doGet(e) {
  try {
    // --- รับ parameters ---
    var temperature = e.parameter.temperature;
    var humidity    = e.parameter.humidity;
    var boardId     = e.parameter.board_id;
    var isQueued    = (e.parameter.queued === "1");

    // --- ตรวจสอบ required fields ---
    if (!temperature || !boardId) {
      return respond("ERROR: Missing temperature or board_id");
    }

    var tempVal  = parseFloat(temperature);
    var humidVal = parseFloat(humidity) || 0;

    // --- ตรวจสอบช่วงค่า ---
    if (isNaN(tempVal) || tempVal < -55 || tempVal > 125) {
      return respond("ERROR: Invalid temperature value: " + temperature);
    }

    // --- เปิด Spreadsheet ---
    var ss    = SpreadsheetApp.getActiveSpreadsheet();
    var sheet = SHEET_NAME
                  ? (ss.getSheetByName(SHEET_NAME) || ss.getActiveSheet())
                  : ss.getActiveSheet();

    // --- สร้าง Header ถ้ายังไม่มี ---
    if (sheet.getLastRow() === 0) {
      createHeader(sheet);
    }

    // --- Timestamp (Asia/Bangkok) ---
    var now       = new Date();
    var timestamp = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm:ss");

    // --- บันทึกข้อมูล ---
    sheet.appendRow([
      timestamp,
      boardId,
      tempVal,
      humidVal > 0 ? humidVal : "",   // ไม่แสดง 0 สำหรับ DS18B20
      isQueued ? "BUFFERED" : "LIVE"
    ]);

    // --- จัดรูปแบบแถวใหม่ ---
    formatLastRow(sheet, isQueued);

    // --- ลบ rows เก่าถ้าเกิน MAX_ROWS ---
    if (MAX_ROWS > 0) trimOldRows(sheet);

    return respond("OK");

  } catch (err) {
    return respond("ERROR: " + err.toString());
  }
}

// ============================================================
// Helper: สร้าง Header row
// ============================================================
function createHeader(sheet) {
  sheet.appendRow(HEADERS);
  var headerRange = sheet.getRange(1, 1, 1, HEADERS.length);
  headerRange.setBackground("#1a73e8");
  headerRange.setFontColor("#ffffff");
  headerRange.setFontWeight("bold");
  headerRange.setFontSize(11);
  sheet.setFrozenRows(1);

  // ตั้งความกว้างคอลัมน์
  sheet.setColumnWidth(1, 160); // Timestamp
  sheet.setColumnWidth(2, 140); // Board ID
  sheet.setColumnWidth(3, 130); // Temperature
  sheet.setColumnWidth(4, 110); // Humidity
  sheet.setColumnWidth(5, 100); // Data Type
}

// ============================================================
// Helper: จัดสีแถว BUFFERED ให้ต่างจาก LIVE
// ============================================================
function formatLastRow(sheet, isQueued) {
  var lastRow   = sheet.getLastRow();
  var rowRange  = sheet.getRange(lastRow, 1, 1, HEADERS.length);
  if (isQueued) {
    rowRange.setBackground("#fff3cd"); // เหลืองอ่อน = offline buffered
  } else {
    rowRange.setBackground(lastRow % 2 === 0 ? "#f8f9fa" : "#ffffff"); // zebra stripes
  }
}

// ============================================================
// Helper: ลบ rows เก่าเกิน MAX_ROWS (เก็บ header + MAX_ROWS)
// ============================================================
function trimOldRows(sheet) {
  var totalRows = sheet.getLastRow();
  if (totalRows > MAX_ROWS + 1) {
    var rowsToDelete = totalRows - MAX_ROWS - 1;
    sheet.deleteRows(2, rowsToDelete); // ลบหลัง header
    Logger.log("Trimmed " + rowsToDelete + " old rows.");
  }
}

// ============================================================
// Helper: ส่ง HTTP Response กลับไปให้ ESP8266
// ============================================================
function respond(message) {
  return ContentService
    .createTextOutput(message)
    .setMimeType(ContentService.MimeType.TEXT);
}

// ============================================================
// testDoGet: ทดสอบโดยรันใน Apps Script Editor
// ============================================================
function testDoGet() {
  var fakeEvent = {
    parameter: {
      temperature : "28.5",
      humidity    : "65.2",
      board_id    : "BOARD_TEST01",
      queued      : "0"
    }
  };
  var result = doGet(fakeEvent);
  Logger.log("Result: " + result.getContent());
}

function testDoGet_Buffered() {
  var fakeEvent = {
    parameter: {
      temperature : "31.0",
      humidity    : "70.0",
      board_id    : "BOARD_TEST01",
      queued      : "1"  // offline buffered
    }
  };
  var result = doGet(fakeEvent);
  Logger.log("Result (buffered): " + result.getContent());
}
