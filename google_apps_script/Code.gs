/**
 * TempBot - Google Apps Script (With Premium Daily Report & Statistical Charts)
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
 *   สรุป / report / กราฟ [บอร์ด] → สรุปรายงานรายวัน 24 ชม. และกราฟสถิติ
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
var LINE_PUSH_URL   = "https://api.line.me/v2/bot/message/push";
var SETTINGS_SHEET  = "Settings";

var QUICK_REPLY_ITEMS = [
  { label: "🌡️ ล่าสุด", text: "temp" },
  { label: "⚙️ ตั้งค่า", text: "ดูค่า" },
  { label: "📈 สรุป", text: "สรุป" },
  { label: "❓ ช่วย", text: "help" }
];

var DEFAULT_THRESHOLDS = {
  maxTemp: 35.0,
  minTemp: 20.0
};

var DEFAULT_BITMAP = "cat";

// ============================================================
// doGet: รับข้อมูลจาก ESP8266
// ?temperature=28.5&humidity=65.2&board_id=BOARD_A1B2C3[&queued=1]
// ============================================================
function doGet(e) {
  try {
    var temperature    = e.parameter.temperature;
    var humidity       = e.parameter.humidity;
    var boardId        = e.parameter.board_id;
    var isQueued       = (e.parameter.queued === "1");
    var timestampParam = e.parameter.timestamp;

    // ถ้าขอดึงค่า settings ให้ return JSON กลับไป
    if (e.parameter.get_settings === "1") {
      if (!boardId || boardId.trim() === "") {
        return respond("ERROR: Missing board_id");
      }
      var thresholds = getThresholds(boardId.trim());
      return respond(JSON.stringify(thresholds));
    }

    if (!temperature) {
      return respond("ERROR: Missing temperature");
    }
    if (!boardId || boardId.trim() === "") {
      return respond("ERROR: Missing or empty board_id");
    }

    var tempVal  = parseFloat(temperature);
    var humidVal = parseFloat(humidity) || 0;

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
    // ถ้าบอร์ดส่งเวลา Unix Epoch ย้อนหลังมา ให้แปลงเป็นเวลาบันทึกจริง
    if (isQueued && timestampParam) {
      var epoch = parseInt(timestampParam, 10);
      if (!isNaN(epoch) && epoch > 0 && epoch > 1000000000 && epoch <= 9999999999) {
        now = new Date(epoch * 1000);
      }
    }

    var timestamp = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm:ss");

    sheet.appendRow([
      timestamp,
      boardId.trim(),
      tempVal,
      humidVal,
      isQueued ? "BUFFERED" : "LIVE"
    ]);

    formatLastRow(sheet, isQueued);
    if (MAX_ROWS > 0) trimOldRows(sheet);

    // ตรวจสอบ threshold และส่ง LINE notify ถ้าเกินค่าตั้ง
    checkAndNotify(boardId.trim(), tempVal, humidVal);

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

  // บันทึกเป้าหมาย Group ID หรือ User ID ล่าสุด เพื่อใช้ส่งรายงานแบบตั้งเวลาอัตโนมัติ
  var source = event.source;
  var targetId = source.groupId || source.roomId || source.userId;
  if (targetId) {
    PropertiesService.getScriptProperties().setProperty("LINE_TARGET_ID", targetId);
  }

  if (["temp", "อุณหภูมิ", "ล่าสุด", "last", "now", "humid", "ความชื้น"].indexOf(text) !== -1) {
    var response = getLatestEntry();
    replyToLine(replyToken, response);

  } else if (["status", "สถานะ", "ทั้งหมด", "all"].indexOf(text) !== -1) {
    var response = getAllBoardStatus();
    replyToLine(replyToken, response);

  } else if (text.indexOf("สรุป") === 0 || text.indexOf("report") === 0 || text.indexOf("กราฟ") === 0) {
    // แยกพารามิเตอร์ชื่อบอร์ด (ถ้ามี) เช่น "สรุป Chicken03"
    var boardParam = "";
    var parts = rawText.trim().split(/\s+/);
    if (parts.length > 1) {
      boardParam = parts.slice(1).join(" ");
    }

    var report = generateDailyReport(boardParam);
    if (report.chartUrl) {
      var messages = [
        { type: "text", text: report.text },
        {
          type: "image",
          originalContentUrl: report.chartUrl,
          previewImageUrl: report.chartUrl
        }
      ];
      replyToLine(replyToken, messages);
    } else {
      replyToLine(replyToken, report.text);
    }

  } else if (text.indexOf("ตั้ง") === 0) {
    // คำสั่ง: ตั้ง max 35 / ตั้ง min 20 / ตั้ง bitmap fish / ตั้ง max 35 Farm03
    var parts = rawText.trim().split(/\s+/);
    if (parts.length >= 3) {
      var type = parts[1].toLowerCase();
      var value = parts[2];
      var targetBoard = parts.length >= 4 ? parts.slice(3).join(" ") : "DEFAULT";
      
      if (type === "bitmap") {
        // ตรวจสอบค่า bitmap ที่รองรับ
        var validBitmaps = ["cat", "chicken", "fish", "tree"];
        if (validBitmaps.indexOf(value) === -1) {
          replyToLine(replyToken, "❌ Bitmap ไม่รู้จัก\nรองรับ: cat, chicken, fish, tree\nเช่น 'ตั้ง bitmap fish'");
        } else {
          saveThreshold(targetBoard, null, null, value);
          var current = getThresholds(targetBoard);
          replyToLine(replyToken, "✅ ตั้ง Bitmap " + targetBoard + ": " + current.bitmap);
        }
      } else if (type === "max" || type === "min") {
        var numValue = parseFloat(value);
        if (isNaN(numValue)) {
          replyToLine(replyToken, "❌ ค่าไม่ถูกต้อง ลองใหม่ เช่น 'ตั้ง max 35'");
        } else if (type === "max") {
          saveThreshold(targetBoard, numValue, null);
          var current = getThresholds(targetBoard);
          replyToLine(replyToken, "✅ ตั้ง MAX " + targetBoard + ": " + current.maxTemp + " °C");
        } else if (type === "min") {
          saveThreshold(targetBoard, null, numValue);
          var current = getThresholds(targetBoard);
          replyToLine(replyToken, "✅ ตั้ง MIN " + targetBoard + ": " + current.minTemp + " °C");
        }
      } else {
        replyToLine(replyToken, "❌ ไม่รู้จัก ลอง 'ตั้ง max 35', 'ตั้ง min 20' หรือ 'ตั้ง bitmap fish'");
      }
    } else {
      replyToLine(replyToken, "❌ ข้อมูลไม่ครบ\nลอง: ตั้ง max 35, ตั้ง min 20, ตั้ง bitmap fish\nเพิ่มชื่อบอร์ด: ตั้ง bitmap fish Farm03");
    }

  } else if (text === "ดูค่า" || text === "ตั้งค่า" || text === "ค่า") {
    var parts = rawText.trim().split(/\s+/);
    var targetBoard = parts.length >= 2 ? parts.slice(1).join(" ") : "DEFAULT";
    var t = getThresholds(targetBoard);
    var msg = "📋 ค่าตั้งของ " + targetBoard + "\n"
            + "─────────────────\n"
            + "🌡️ MAX: " + t.maxTemp + " °C\n"
            + "🌡️ MIN: " + t.minTemp + " °C\n"
            + "🖼️ Bitmap: " + t.bitmap + "\n"
            + "─────────────────\n"
            + "เปลี่ยน: ตั้ง max 35, ตั้ง min 20, ตั้ง bitmap fish";
    replyToLine(replyToken, msg);

  } else if (["help", "ช่วยเหลือ", "คำสั่ง", "?"].indexOf(text) !== -1) {
    var response = "📋 TempBot คำสั่งที่ใช้ได้\n"
                 + "─────────────────\n"
                 + "• temp / อุณหภูมิ / ความชื้น / ล่าสุด\n"
                 + "  → ข้อมูลอุณหภูมิและความชื้นล่าสุด\n\n"
                 + "• status / สถานะ / ทั้งหมด\n"
                 + "  → สรุปทุกบอร์ด\n\n"
                 + "• สรุป / report / กราฟ [บอร์ด]\n"
                 + "  → รายงานสรุปรายวัน 24 ชม. และกราฟสถิติ\n\n"
                 + "• ตั้ง max 35 / ตั้ง min 20 [บอร์ด]\n"
                 + "  → ตั้งค่าแจ้งเตือนอุณหภูมิ\n\n"
                 + "• ตั้ง bitmap fish [บอร์ด]\n"
                 + "  → ตั้งรูปแสดงผล (cat/chicken/fish/tree)\n\n"
                 + "• ดูค่า / ดูค่า [บอร์ด]\n"
                 + "  → ดูค่าตั้งปัจจุบัน\n\n"
                 + "• help / ช่วยเหลือ\n"
                 + "  → แสดงคำสั่งนี้";
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

  var formattedTime = Utilities.formatDate(new Date(timestamp), TIMEZONE, "dd MMM. yy HH:mm") + " น.";
  
  var msg = "🌡️ ข้อมูลล่าสุด\n";
  msg += "📟 " + boardId + "\n";
  msg += "🌡️ อุณหภูมิ: " + temp + " °C\n";
  if (humid !== "" && humid !== null && humid !== undefined) {
    msg += "💧 ความชื้น: " + humid + " %\n";
  }
  msg += "🕐 " + formattedTime;
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

  var allData = sheet.getRange(2, 1, lastRow - 1, 5).getValues();

  var latestPerBoard = new Map();
  for (var i = allData.length - 1; i >= 0; i--) {
    var boardId = String(allData[i][1] || "");
    if (boardId && !latestPerBoard.has(boardId)) {
      latestPerBoard.set(boardId, allData[i]);
    }
  }

  if (latestPerBoard.size === 0) return "❌ ไม่พบข้อมูลบอร์ด";

  var msg = "📊 สถานะทุกบอร์ด (" + latestPerBoard.size + " บอร์ด)\n";
  msg += "─────────────────\n";

  var boards = Array.from(latestPerBoard.keys());
  for (var b = 0; b < boards.length; b++) {
    var d = latestPerBoard.get(boards[b]);
    var boardTime = Utilities.formatDate(new Date(d[0]), TIMEZONE, "dd MMM. yy HH:mm") + " น.";
    msg += "📟 " + d[1] + "\n";
    msg += "🌡️ " + d[2] + " °C";
    if (d[3] !== "" && d[3] !== 0) {
      msg += "  💧 " + d[3] + " %";
    }
    msg += "\n🕐 " + boardTime;
    if (b < boards.length - 1) msg += "\n─────────────────\n";
  }

  return msg;
}

// ============================================================
// generateDailyReport: คำนวณสถิติ 24 ชม. และสร้างลิงก์กราฟ QuickChart
// ============================================================
function generateDailyReport(boardId) {
  var sheet = getSheet();
  var lastRow = sheet.getLastRow();
  if (lastRow <= 1) {
    return { text: "❌ ยังไม่มีข้อมูลในระบบ", chartUrl: null };
  }

  // 1. ดึงข้อมูลทั้งหมด
  var data = sheet.getRange(2, 1, lastRow - 1, 5).getValues();
  var now = new Date();
  var oneDayAgo = new Date(now.getTime() - 24 * 60 * 60 * 1000);

  // 2. กรองข้อมูลเฉพาะ 24 ชม. ล่าสุดของบอร์ดเป้าหมาย
  var targetBoard = boardId ? boardId.trim() : "";
  
  // หากไม่ได้ระบุชื่อบอร์ด ให้หาบอร์ดล่าสุดที่มีการส่งข้อมูลเข้ามาใน 24 ชั่วโมง
  if (targetBoard === "") {
    for (var i = data.length - 1; i >= 0; i--) {
      var rowDate = new Date(data[i][0]);
      if (rowDate >= oneDayAgo && data[i][1]) {
        targetBoard = String(data[i][1]);
        break;
      }
    }
  }

  if (targetBoard === "") {
    return { text: "❌ ไม่พบข้อมูลบอร์ดใดๆ ในช่วง 24 ชั่วโมงที่ผ่านมา", chartUrl: null };
  }

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

  if (filtered.length === 0) {
    return { text: "❌ ไม่พบข้อมูลสำหรับบอร์ด \"" + targetBoard + "\" ในช่วง 24 ชั่วโมงที่ผ่านมา", chartUrl: null };
  }

  // นำชื่อจริงของบอร์ดมาแสดง
  var realBoardName = targetBoard;
  for (var i = data.length - 1; i >= 0; i--) {
    if (data[i][1] && String(data[i][1]).toLowerCase() === targetLower) {
      realBoardName = String(data[i][1]);
      break;
    }
  }

  // 3. คำนวณสถิติ (Min, Max, Avg)
  var minTemp = 999, maxTemp = -999, sumTemp = 0, countTemp = 0;
  var minHumid = 999, maxHumid = -999, sumHumid = 0, countHumid = 0;

  for (var i = 0; i < filtered.length; i++) {
    var t = filtered[i].temp;
    var h = filtered[i].humid;

    if (t !== null && !isNaN(t) && t >= -55 && t <= 125) {
      if (t < minTemp) minTemp = t;
      if (t > maxTemp) maxTemp = t;
      sumTemp += t;
      countTemp++;
    }

    if (h !== null && !isNaN(h) && h >= 0 && h <= 100) {
      if (h < minHumid) minHumid = h;
      if (h > maxHumid) maxHumid = h;
      sumHumid += h;
      countHumid++;
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

  // 4. Downsampling ข้อมูลให้กลายเป็น 24 จุดชั่วโมง (เพื่อให้กราฟอ่านง่ายและ URL ไม่ยาวเกินไป)
  var buckets = [];
  for (var h = 23; h >= 0; h--) {
    var bucketTime = new Date(now.getTime() - h * 60 * 60 * 1000);
    buckets.push({
      label: Utilities.formatDate(bucketTime, TIMEZONE, "HH:00"),
      startTime: new Date(bucketTime.getFullYear(), bucketTime.getMonth(), bucketTime.getDate(), bucketTime.getHours(), 0, 0),
      endTime: new Date(bucketTime.getFullYear(), bucketTime.getMonth(), bucketTime.getDate(), bucketTime.getHours(), 59, 59),
      temps: [],
      humids: []
    });
  }

  // จัดข้อมูลใส่กลุ่มตามชั่วโมง
  for (var i = 0; i < filtered.length; i++) {
    var pt = filtered[i];
    var ptTimeMs = pt.time.getTime();
    for (var b = 0; b < buckets.length; b++) {
      var bucketStartMs = buckets[b].startTime.getTime();
      var bucketEndMs = buckets[b].endTime.getTime() + 999; // inclusive end
      if (ptTimeMs >= bucketStartMs && ptTimeMs <= bucketEndMs) {
        if (pt.temp !== null && !isNaN(pt.temp)) buckets[b].temps.push(pt.temp);
        if (pt.humid !== null && !isNaN(pt.humid)) buckets[b].humids.push(pt.humid);
        break;
      }
    }
  }

  var labels = [];
  var tempData = [];
  var humidData = [];
  var hasHumid = false;

  for (var b = 0; b < buckets.length; b++) {
    labels.push(buckets[b].label);

    if (buckets[b].temps.length > 0) {
      var avg = buckets[b].temps.reduce(function(x, y) { return x + y; }, 0) / buckets[b].temps.length;
      tempData.push(parseFloat(avg.toFixed(1)));
    } else {
      // ใช้ค่าล่าสุดที่บันทึกได้ เพื่อป้องกันไม่ให้กราฟตกไปที่ 0
      tempData.push(tempData.length > 0 ? tempData[tempData.length - 1] : null);
    }

    if (buckets[b].humids.length > 0) {
      var avg = buckets[b].humids.reduce(function(x, y) { return x + y; }, 0) / buckets[b].humids.length;
      humidData.push(parseFloat(avg.toFixed(1)));
      hasHumid = true;
    } else {
      humidData.push(humidData.length > 0 ? humidData[humidData.length - 1] : null);
    }
  }

  // 5. โครงสร้างตั้งค่ากราฟ QuickChart
  var chartConfig = {
    type: 'line',
    data: {
      labels: labels,
      datasets: [
        {
          label: 'Temperature (°C)',
          borderColor: '#ff6384',
          backgroundColor: 'rgba(255, 99, 132, 0.08)',
          data: tempData,
          yAxisID: 'yTemp',
          fill: true,
          tension: 0.4,
          borderWidth: 3,
          pointRadius: 1.5
        }
      ]
    },
    options: {
      title: {
        display: true,
        text: 'Daily Report: ' + realBoardName + ' (Last 24 Hours)',
        fontSize: 14,
        fontStyle: 'bold'
      },
      legend: {
        display: true,
        position: 'bottom',
        labels: { fontSize: 10 }
      },
      scales: {
        xAxes: [{
          gridLines: { display: false },
          ticks: { fontSize: 8, maxTicksLimit: 12 }
        }],
        yAxes: [
          {
            id: 'yTemp',
            type: 'linear',
            position: 'left',
            scaleLabel: { display: true, labelString: 'Temperature (°C)', fontSize: 10 },
            ticks: { fontSize: 8 }
          }
        ]
      }
    }
  };

  // ถ้ามีข้อมูลความชื้น (DHT22) ให้ทำกราฟแบบ 2 แกนคู่ Y-Axis
  if (hasHumid && countHumid > 0) {
    chartConfig.data.datasets.push({
      label: 'Humidity (%)',
      borderColor: '#36a2eb',
      backgroundColor: 'rgba(54, 162, 235, 0.04)',
      data: humidData,
      yAxisID: 'yHumid',
      fill: true,
      tension: 0.4,
      borderWidth: 3,
      pointRadius: 1.5
    });
    chartConfig.options.scales.yAxes.push({
      id: 'yHumid',
      type: 'linear',
      position: 'right',
      scaleLabel: { display: true, labelString: 'Humidity (%)', fontSize: 10 },
      ticks: { min: 0, max: 100, fontSize: 8 },
      gridLines: { drawOnChartArea: false }
    });
  }

  var chartUrl = "";
  try {
    var shortenerRes = UrlFetchApp.fetch("https://quickchart.io/chart/create", {
      method: "POST",
      contentType: "application/json",
      payload: JSON.stringify({
        width: 600,
        height: 380,
        backgroundColor: "white",
        chart: chartConfig
      }),
      muteHttpExceptions: true
    });
    
    if (shortenerRes.getResponseCode() === 200) {
      var resJson = JSON.parse(shortenerRes.getContentText());
      if (resJson.success) {
        chartUrl = resJson.url;
        Logger.log("QuickChart Short URL generated: " + chartUrl);
      }
    }
  } catch (e) {
    Logger.log("QuickChart shortener failed: " + e.toString());
  }

  // Fallback ในกรณีที่ Shortener ล้มเหลว
  if (!chartUrl) {
    chartUrl = "https://quickchart.io/chart?w=600&h=380&bkg=white&c=" + encodeURIComponent(JSON.stringify(chartConfig));
  }

  // 6. ประกอบข้อความรายงานสรุป
  var msg = "📊 " + realBoardName + " - สรุป 24 ชม.\n";
  msg += "──────────────────\n";
  msg += "🌡️ อุณหภูมิ: " + minTempStr.replace("°C", " °C") + " - " + maxTempStr.replace("°C", " °C") + " (เฉลี่ย " + avgTempStr.replace("°C", " °C") + ")\n";

  if (hasHumid && countHumid > 0) {
    msg += "💧 ความชื้น: " + minHumidStr.replace("%", " %") + " - " + maxHumidStr.replace("%", " %") + " (เฉลี่ย " + avgHumidStr.replace("%", " %") + ")\n";
  }

  msg += "📈 บันทึก " + filtered.length + " ครั้ง";
  msg += "\n──────────────────";

  return { text: msg, chartUrl: chartUrl, boardId: realBoardName };
}

// ============================================================
// sendDailyReportPush: ส่งสรุปรายวันอัตโนมัติไปยัง LINE_TARGET_ID
// ============================================================
function sendDailyReportPush() {
  try {
    var targetId = PropertiesService.getScriptProperties().getProperty("LINE_TARGET_ID");
    var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");

    if (!targetId || !token) {
      Logger.log("ERROR: LINE_TARGET_ID or LINE_TOKEN not found in Script Properties!");
      return;
    }

    var sheet = getSheet();
    var lastRow = sheet.getLastRow();
    if (lastRow <= 1) return;

    // หาบอร์ดที่มีความเคลื่อนไหวใน 24 ชม. ล่าสุด
    var data = sheet.getRange(2, 1, lastRow - 1, 5).getValues();
    var now = new Date();
    var oneDayAgo = new Date(now.getTime() - 24 * 60 * 60 * 1000);

    var activeBoards = new Map();
    for (var i = 0; i < data.length; i++) {
      var rowDate = new Date(data[i][0]);
      if (rowDate >= oneDayAgo && data[i][1]) {
        var bId = String(data[i][1]);
        if (!activeBoards.has(bId)) activeBoards.set(bId, true);
      }
    }

    var boards = Array.from(activeBoards.keys());
    if (boards.length === 0) {
      Logger.log("No active boards found in the last 24 hours.");
      return;
    }

    // ส่งรายงานรายบอร์ดแยกอิสระ
    for (var b = 0; b < boards.length; b++) {
      var report = generateDailyReport(boards[b]);
      var messages = [];

      if (report.chartUrl) {
        messages = [
          { type: "text", text: report.text },
          {
            type: "image",
            originalContentUrl: report.chartUrl,
            previewImageUrl: report.chartUrl
          }
        ];
      } else {
        messages = [{ type: "text", text: report.text }];
      }

      pushToLine(targetId, messages);
      Utilities.sleep(1500); // ดีเลย์เพื่อป้องกันการสลับลำดับข้อความ
    }

  } catch (err) {
    Logger.log("sendDailyReportPush error: " + err.toString());
  }
}

// ============================================================
// pushToLine: ส่งข้อความแบบ Push ไปยังกลุ่ม/ผู้ใช้ปลายทาง
// ============================================================
function pushToLine(targetId, messages, retries) {
  var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");
  if (!token) return;

  retries = retries || 0;
  var messageArray = Array.isArray(messages) ? messages : [messages];

  var payload = JSON.stringify({
    to: targetId,
    messages: messageArray
  });

  var options = {
    method      : "POST",
    contentType : "application/json",
    headers     : { "Authorization": "Bearer " + token },
    payload     : payload,
    muteHttpExceptions: true
  };

  try {
    var res = UrlFetchApp.fetch(LINE_PUSH_URL, options);
    var responseCode = res.getResponseCode();
    Logger.log("LINE push HTTP " + responseCode + ": " + res.getContentText());
    
    if (responseCode === 429 && retries < 2) {
      Utilities.sleep(1500 * (retries + 1));
      pushToLine(targetId, messages, retries + 1);
    }
  } catch (err) {
    Logger.log("LINE push error: " + err);
    if (retries < 2) {
      Utilities.sleep(1500);
      pushToLine(targetId, messages, retries + 1);
    }
  }
}

// ============================================================
// replyToLine: ส่ง reply กลับ LINE โดยใช้ replyToken
// (รองรับการส่งข้อความหลายประเภทพร้อมกัน)
// ============================================================
function replyToLine(replyToken, messages, retries) {
  var token = PropertiesService.getScriptProperties().getProperty("LINE_TOKEN");
  if (!token) {
    Logger.log("ERROR: LINE_TOKEN not found in Script Properties!");
    return;
  }

  retries = retries || 0;
  var messageArray = [];
  var quickReplyItems = QUICK_REPLY_ITEMS.map(function(item) {
    return {
      type: "action",
      action: { type: "message", label: item.label, text: item.text }
    };
  });

  if (Array.isArray(messages)) {
    messageArray = messages;
  } else if (typeof messages === "string") {
    messageArray = [{ type: "text", text: messages }];
  } else {
    messageArray = [messages];
  }

  // Add quick reply to text messages
  for (var i = 0; i < messageArray.length; i++) {
    if (messageArray[i].type === "text") {
      messageArray[i].quickReply = { items: quickReplyItems };
    }
  }

  var payload = JSON.stringify({
    replyToken: replyToken,
    messages: messageArray
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
    var responseCode = res.getResponseCode();
    Logger.log("LINE reply HTTP " + responseCode + ": " + res.getContentText());
    
    if (responseCode === 429 && retries < 2) {
      Utilities.sleep(1500 * (retries + 1));
      replyToLine(replyToken, messages, retries + 1);
    }
  } catch (err) {
    Logger.log("LINE reply error: " + err);
    if (retries < 2) {
      Utilities.sleep(1500);
      replyToLine(replyToken, messages, retries + 1);
    }
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
// Helper: ดึง Settings Sheet
// ============================================================
function getSettingsSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = ss.getSheetByName(SETTINGS_SHEET);
  if (!sheet) {
    sheet = ss.insertSheet(SETTINGS_SHEET);
    sheet.appendRow(["Board ID", "Max Temp (°C)", "Min Temp (°C)", "Bitmap", "Updated"]);
    sheet.getRange(1, 1, 1, 5).setBackground("#1a73e8").setFontColor("#ffffff").setFontWeight("bold");
    sheet.setColumnWidth(1, 150);
    sheet.setColumnWidth(2, 120);
    sheet.setColumnWidth(3, 120);
    sheet.setColumnWidth(4, 100);
    sheet.setColumnWidth(5, 160);
  }
  return sheet;
}

// ============================================================
// Helper: ดึง threshold และ bitmap ของบอร์ด
// ============================================================
function getThresholds(boardId) {
  var sheet = getSettingsSheet();
  var data = sheet.getDataRange().getValues();
  
  var result = {
    maxTemp: DEFAULT_THRESHOLDS.maxTemp,
    minTemp: DEFAULT_THRESHOLDS.minTemp,
    maxHumid: 80.0,
    minHumid: 30.0,
    bitmap: DEFAULT_BITMAP
  };
  
  // หาค่าเฉพาะของบอร์ด
  for (var i = 1; i < data.length; i++) {
    var rowBoardId = String(data[i][0] || "").trim();
    if (rowBoardId === boardId) {
      if (data[i][1] !== "") result.maxTemp = parseFloat(data[i][1]);
      if (data[i][2] !== "") result.minTemp = parseFloat(data[i][2]);
      if (data[i][3] !== "") result.bitmap = String(data[i][3]).trim();
      break;
    }
  }
  
  return result;
}

// ============================================================
// Helper: บันทึก threshold และ bitmap
// ============================================================
function saveThreshold(boardId, maxTemp, minTemp, bitmap) {
  var sheet = getSettingsSheet();
  var data = sheet.getDataRange().getValues();
  var now = new Date();
  var formattedTime = Utilities.formatDate(now, TIMEZONE, "yyyy-MM-dd HH:mm");
  
  // หา row ของบอร์ด
  var rowIndex = -1;
  for (var i = 1; i < data.length; i++) {
    if (String(data[i][0] || "").trim() === boardId) {
      rowIndex = i + 1; // 1-based index
      break;
    }
  }
  
  if (rowIndex > 0) {
    // อ่านค่าเดิมของ bitmap ถ้าไม่ได้ส่งมา
    var existingBitmap = data[rowIndex - 1][3] || DEFAULT_BITMAP;
    var bitmapToSave = (bitmap !== undefined && bitmap !== null) ? bitmap : existingBitmap;
    sheet.getRange(rowIndex, 2, 1, 4).setValues([[maxTemp, minTemp, bitmapToSave, formattedTime]]);
  } else {
    var bitmapToSave = (bitmap !== undefined && bitmap !== null) ? bitmap : DEFAULT_BITMAP;
    sheet.appendRow([boardId, maxTemp, minTemp, bitmapToSave, formattedTime]);
  }
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
  if (totalRows > (MAX_ROWS + 1)) {
    var rowsToDelete = totalRows - MAX_ROWS - 1;
    sheet.deleteRows(2, rowsToDelete);
    Logger.log("ระบบได้ลบข้อมูลเก่าที่เกินกำหนดออกแล้ว จำนวน " + rowsToDelete + " แถว");
  }
}

// ============================================================
// checkAndNotify: ตรวจสอบ threshold และส่ง LINE notify
// ============================================================
var ALERT_COOLDOWN_MS = 1800000; // 30 นาที
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
  
  // อ่านสถานะล่าสุดของบอร์ด
  var lastStateKey = "LAST_ALERT_STATE_" + boardId;
  var lastTimeKey = "LAST_NOTIFY_TIME_" + boardId;
  var lastTempStateKey = "LAST_TEMP_STATE_" + boardId;
  var lastHumidStateKey = "LAST_HUMID_STATE_" + boardId;
  
  var lastState = props.getProperty(lastStateKey) || "NORMAL";
  var lastNotifyTime = parseInt(props.getProperty(lastTimeKey) || "0");
  var lastTempState = props.getProperty(lastTempStateKey) || "NORMAL";
  var lastHumidState = props.getProperty(lastHumidStateKey) || "NORMAL";
  
  // กำหนดสถานะใหม่
  var newTempState = "NORMAL";
  if (temp <= minT && temp > -50) {
    newTempState = "LOW";
  } else if (temp >= maxT) {
    newTempState = "HIGH";
  } else {
    if (lastTempState === "LOW" && temp < minT + HYSTERESIS_TEMP) {
      newTempState = "LOW";
    } else if (lastTempState === "HIGH" && temp > maxT - HYSTERESIS_TEMP) {
      newTempState = "HIGH";
    }
  }
  
  var newHumidState = "NORMAL";
  // DS18B20 ไม่มีความชื้น (humid = 0) ข้ามการตรวจสอบความชื้น
  if (humid > 0) {
    if (humid <= minH) {
      newHumidState = "LOW";
    } else if (humid >= maxH) {
      newHumidState = "HIGH";
    } else {
      if (lastHumidState === "LOW" && humid < minH + HYSTERESIS_HUMID) {
        newHumidState = "LOW";
      } else if (lastHumidState === "HIGH" && humid > maxH - HYSTERESIS_HUMID) {
        newHumidState = "HIGH";
      }
    }
  }
  
  // สร้างข้อความแจ้งเตือน
  var messages = [];
  var newState = "NORMAL";
  
  if (newTempState !== "NORMAL") {
    if (newTempState === "LOW") {
      messages.push("⚠️ แจ้งเตือน: อุณหภูมิต่ำกว่าค่าตั้ง\n🌡️ อุณหภูมิปัจจุบัน: " + temp.toFixed(1) + " °C\n📉 ค่าต่ำสุด: " + minT + " °C\n📟 บอร์ด: " + boardId);
    } else {
      messages.push("⚠️ แจ้งเตือน: อุณหภูมิสูงกว่าค่าตั้ง\n🌡️ อุณหภูมิปัจจุบัน: " + temp.toFixed(1) + " °C\n📈 ค่าสูงสุด: " + maxT + " °C\n📟 บอร์ด: " + boardId);
    }
    newState = "ALERT";
  }
  
  if (newHumidState !== "NORMAL") {
    if (newHumidState === "LOW") {
      messages.push("⚠️ แจ้งเตือน: ความชื้นต่ำกว่าค่าตั้ง\n💧 ความชื้นปัจจุบัน: " + humid.toFixed(1) + " %\n📉 ค่าต่ำสุด: " + minH + " %\n📟 บอร์ด: " + boardId);
    } else {
      messages.push("⚠️ แจ้งเตือน: ความชื้นสูงกว่าค่าตั้ง\n💧 ความชื้นปัจจุบัน: " + humid.toFixed(1) + " %\n📈 ค่าสูงสุด: " + maxH + " %\n📟 บอร์ด: " + boardId);
    }
    newState = "ALERT";
  }
  
  // ถ้ากลับมาปกติ
  if (newTempState === "NORMAL" && newHumidState === "NORMAL" && (lastTempState !== "NORMAL" || lastHumidState !== "NORMAL")) {
    messages.push("✅ กลับมาปกติแล้ว\n🌡️ " + temp.toFixed(1) + " °C  |  💧 " + humid.toFixed(1) + " %\n📟 บอร์ด: " + boardId);
    newState = "NORMAL";
  }
  
  // ตรวจสอบว่าต้องส่งหรือไม่
  var shouldSend = false;
  if (messages.length > 0) {
    if (newState !== lastState) {
      shouldSend = true; // เปลี่ยนสถานะ = ส่งทันที
    } else if (newState !== "NORMAL" && (now - lastNotifyTime) >= ALERT_COOLDOWN_MS) {
      shouldSend = true; // ยังอยู่ในสถานะเดิม + เกิน cooldown
    }
  }
  
  if (shouldSend && messages.length > 0) {
    // ดึง targetId จากครั้งก่อนที่มีคนส่งข้อความมา
    var targetId = props.getProperty("LINE_TARGET_ID");
    var token = props.getProperty("LINE_TOKEN");
    
    if (targetId && token && messages.length > 0) {
      for (var i = 0; i < messages.length; i++) {
        pushNotifyToLine(targetId, messages[i], token);
        Utilities.sleep(500);
      }
      props.setProperty(lastTimeKey, now.toString());
    }
  }
  
  // บันทึกสถานะ
  props.setProperty(lastStateKey, newState);
  props.setProperty(lastTempStateKey, newTempState);
  props.setProperty(lastHumidStateKey, newHumidState);
}

function pushNotifyToLine(targetId, message, token) {
  var options = {
    method: "POST",
    contentType: "application/json",
    headers: { "Authorization": "Bearer " + token },
    payload: JSON.stringify({
      to: targetId,
      messages: [{ type: "text", text: message }]
    }),
    muteHttpExceptions: true
  };
  
  try {
    var res = UrlFetchApp.fetch(LINE_PUSH_URL, options);
    Logger.log("LINE Notify: " + res.getResponseCode() + " - " + message.substring(0, 50));
  } catch (e) {
    Logger.log("LINE Notify error: " + e.toString());
  }
}

// ============================================================
// TEST FUNCTIONS (รันใน Apps Script Editor เพื่อทดสอบระบบได้)
// ============================================================
function testDoGet() {
  var result = doGet({ parameter: {
    temperature: "28.5", humidity: "65.2",
    board_id: "BOARD_TEST01", queued: "0"
  }});
  Logger.log(result.getContent());
}

function testDailyReport_Offline() {
  var report = generateDailyReport();
  Logger.log("Text Report:\n" + report.text);
  Logger.log("Chart URL:\n" + report.chartUrl);
}
