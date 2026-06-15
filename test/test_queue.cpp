// Offline Queue Tests — TempBot
// Build & Run: make -C test queue
// Or directly: g++ -std=c++14 -o test/test_queue test/test_queue.cpp && ./test/test_queue

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <string>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>

// ============================================================
// Arduino String stub  (ซ้ำจาก run_tests.cpp แต่ standalone)
// ============================================================
class String {
public:
    std::string _s;
    String() {}
    String(const char* c) : _s(c ? c : "") {}
    String(std::string s) : _s(std::move(s)) {}
    String(int v, int base=10) {
        std::ostringstream ss;
        if(base==16) ss<<std::hex<<v; else ss<<v; _s=ss.str();
    }
    String(long v)         { _s=std::to_string(v); }
    String(unsigned long v){ _s=std::to_string(v); }
    String(float v, int d=1){ char b[32]; snprintf(b,32,"%.*f",d,v); _s=b; }

    size_t      length()   const { return _s.length(); }
    const char* c_str()    const { return _s.c_str(); }
    char        charAt(int i) const { return _s[i]; }
    int         toInt()    const { return atoi(_s.c_str()); }
    bool        isEmpty()  const { return _s.empty(); }
    void        trim() {
        size_t l=_s.find_first_not_of(" \t\r\n"), r=_s.find_last_not_of(" \t\r\n");
        _s=(l==std::string::npos)?"":_s.substr(l,r-l+1);
    }
    int indexOf(char c, int from=0) const {
        size_t p=_s.find(c,from); return p==std::string::npos?-1:(int)p;
    }
    String substring(int from, int to=-1) const {
        return to<0 ? String(_s.substr(from)) : String(_s.substr(from,to-from));
    }
    bool startsWith(const char* p) const { return _s.rfind(p,0)==0; }
    bool operator==(const String& o) const { return _s==o._s; }
    bool operator==(const char* o)   const { return _s==o; }
    bool operator!=(const String& o) const { return _s!=o._s; }
    String  operator+(const String& o) const { return String(_s+o._s); }
    String  operator+(const char* o)   const { return String(_s+o); }
    String& operator+=(const String& o){ _s+=o._s; return *this; }
    String& operator+=(char c)         { _s+=c;    return *this; }
    friend String operator+(const char* l,const String& r){ return String(std::string(l)+r._s); }
};

// ============================================================
// LittleFS in-memory mock
// ============================================================
static std::map<std::string, std::string> _fs;

class MockFile {
    std::string      _path;
    std::istringstream _rs;
    std::string      _wbuf;
    char             _mode; // 'r' 'w' 'a'
    bool             _valid;
public:
    MockFile() : _mode(0), _valid(false) {}
    MockFile(const std::string& path, char mode)
        : _path(path), _mode(mode), _valid(true) {
        if (mode=='r') {
            std::string content = _fs.count(path) ? _fs[path] : "";
            _rs.str(content);
        } else if (mode=='a') {
            _wbuf = _fs.count(path) ? _fs[path] : "";
        }
        // mode=='w' → _wbuf starts empty
    }
    operator bool() const { return _valid; }
    bool available() { return _rs.peek() != std::istringstream::traits_type::eof(); }
    String readStringUntil(char c) {
        std::string line; std::getline(_rs, line, c); return String(line);
    }
    int read() { char c; return _rs.get(c) ? (unsigned char)c : -1; }
    size_t size() { return _fs.count(_path) ? _fs[_path].size() : 0; }
    void println(const String& s) { _wbuf += s._s; _wbuf += '\n'; }
    void print(const String& s)   { _wbuf += s._s; }
    void close() {
        if (_mode=='w' || _mode=='a') _fs[_path] = _wbuf;
    }
};
typedef MockFile File;

struct MockFS_t {
    bool begin()                    { return true; }
    bool exists(const char* p)      { return _fs.count(std::string(p)) > 0; }
    bool remove(const char* p)      { _fs.erase(std::string(p)); return true; }
    File open(const char* p, const char* mode) {
        if (mode[0]=='r') return File(p,'r');
        if (mode[0]=='w') { _fs[p]=""; return File(p,'w'); }
        if (mode[0]=='a') return File(p,'a');
        return File();
    }
} LittleFS;

void lfs_reset() { _fs.clear(); }
std::string lfs_read(const char* path) { return _fs.count(path)?_fs[path]:""; }
void lfs_write(const char* path, const std::string& content) { _fs[path]=content; }

// ============================================================
// Stubs for Arduino / hardware calls
// ============================================================
static time_t  _mock_time = 1700000000; // valid epoch
static time_t mock_time_fn() { return _mock_time; }
#define time(x) mock_time_fn()

static unsigned long _mock_millis = 0;
unsigned long millis() { return _mock_millis; }

namespace ESP { inline void wdtFeed(){} }
struct { int status() const { return 3; } } WiFi; // WL_CONNECTED=3
#define WL_CONNECTED 3

// ============================================================
// Globals required by queue functions
// ============================================================
#define QUEUE_FILE        "/queue.csv"
#define MAX_QUEUE_ENTRIES 96

unsigned long lastSyncTimeEpoch  = 0;
unsigned long lastSyncTimeMillis = 0;

// ============================================================
// Queue functions  (copied verbatim from .ino)
// ============================================================
int getQueueSize() {
    if (!LittleFS.exists(QUEUE_FILE)) return 0;
    File f = LittleFS.open(QUEUE_FILE, "r");
    if (!f) return 0;
    int count = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 2) count++;
    }
    f.close();
    return count;
}

// DS18B20 variant (temp only)
void queueData_DS18B20(float temp) {
    int size = getQueueSize();
    if (size >= MAX_QUEUE_ENTRIES) {
        File f = LittleFS.open(QUEUE_FILE, "r");
        if (f) {
            String remaining = "";
            f.readStringUntil('\n');
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() > 2) remaining += line + "\n";
            }
            f.close();
            File fw = LittleFS.open(QUEUE_FILE, "w");
            if (fw) { fw.print(remaining); fw.close(); }
        }
        size = getQueueSize();
    }
    File f = LittleFS.open(QUEUE_FILE, "a");
    if (f) {
        time_t now = time(nullptr);
        if (now < 1000000000 && lastSyncTimeEpoch >= 1000000000) {
            unsigned long elapsed = (millis() - lastSyncTimeMillis) / 1000;
            now = lastSyncTimeEpoch + elapsed;
        }
        f.println(String((long)now) + "," + String(temp, 1));
        f.close();
    }
}

// DHT22 variant (temp + humid)
void queueData_DHT22(float temp, float humid) {
    int size = getQueueSize();
    if (size >= MAX_QUEUE_ENTRIES) {
        File f = LittleFS.open(QUEUE_FILE, "r");
        if (f) {
            String remaining = "";
            f.readStringUntil('\n');
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() > 2) remaining += line + "\n";
            }
            f.close();
            File fw = LittleFS.open(QUEUE_FILE, "w");
            if (fw) { fw.print(remaining); fw.close(); }
        }
        size = getQueueSize();
    }
    File f = LittleFS.open(QUEUE_FILE, "a");
    if (f) {
        time_t now = time(nullptr);
        if (now < 1000000000 && lastSyncTimeEpoch >= 1000000000) {
            unsigned long elapsed = (millis() - lastSyncTimeMillis) / 1000;
            now = lastSyncTimeEpoch + elapsed;
        }
        f.println(String((long)now) + "," + String(temp, 1) + "," + String(humid, 1));
        f.close();
    }
}

// ============================================================
// CSV parsing helpers  (extracted from flushQueue inline logic)
// ============================================================
struct QueueEntry03 { String timestamp, temp; bool valid; };
struct QueueEntry02 { String timestamp, temp, humid; bool valid; };

QueueEntry03 parseEntry03(String line) {
    QueueEntry03 e{"","",false};
    int c1 = line.indexOf(',');
    if (c1 < 0) return e;
    // DS18B20 format: timestamp,temp[,ignored]  (first field always timestamp)
    e.valid     = true;
    e.timestamp = line.substring(0, c1);
    int c2      = line.indexOf(',', c1+1);
    e.temp      = (c2 < 0) ? line.substring(c1+1) : line.substring(c1+1, c2);
    return e;
}

QueueEntry02 parseEntry02(String line) {
    QueueEntry02 e{"","","",false};
    int c1 = line.indexOf(',');
    if (c1 < 0) return e;
    int c2 = line.indexOf(',', c1+1);
    if (c2 < 0) {
        // old format: temp,humid (no timestamp)
        e.temp  = line.substring(0, c1);
        e.humid = line.substring(c1+1);
        e.valid = true;
    } else {
        e.timestamp = line.substring(0, c1);
        e.temp      = line.substring(c1+1, c2);
        e.humid     = line.substring(c2+1);
        e.valid = true;
    }
    return e;
}

// ============================================================
// Test framework
// ============================================================
static int _passed=0, _failed=0;
#define TEST(name,expr) do { \
    bool _ok=(expr); \
    if(_ok){ printf("  \033[32m✓ %s\033[0m\n",name); _passed++; } \
    else   { printf("  \033[31m✗ FAIL: %s\033[0m  (line %d)\n",name,__LINE__); _failed++; } \
} while(0)
#define SECTION(name) printf("\n\033[1m── %s\033[0m\n",name)

// helper: get Nth line (0-indexed) from queue file
std::string queue_line(int n) {
    std::string content = lfs_read(QUEUE_FILE);
    std::istringstream ss(content);
    std::string line;
    for (int i=0; std::getline(ss, line); i++) if(i==n) return line;
    return "";
}

// ============================================================
// Test suites
// ============================================================

void test_getQueueSize() {
    SECTION("getQueueSize");
    lfs_reset();

    TEST("no file → 0",           getQueueSize() == 0);

    lfs_write(QUEUE_FILE, "");
    TEST("empty file → 0",        getQueueSize() == 0);

    lfs_write(QUEUE_FILE, "1700000000,28.5\n");
    TEST("1 valid line → 1",      getQueueSize() == 1);

    lfs_write(QUEUE_FILE, "1700000000,28.5\n1700000001,29.0\n1700000002,27.5\n");
    TEST("3 valid lines → 3",     getQueueSize() == 3);

    // บรรทัดสั้นกว่า 3 chars ไม่นับ (length > 2 condition)
    lfs_write(QUEUE_FILE, "1700000000,28.5\n\n  \n1700000001,29.0\n");
    TEST("blank lines ignored",   getQueueSize() == 2);

    // บรรทัดที่มีแค่ 2 chars ไม่นับ
    lfs_write(QUEUE_FILE, "ok\n1700000000,28.5\n");
    TEST("2-char line not counted", getQueueSize() == 1);
}

void test_queueData_DS18B20() {
    SECTION("queueData  DS18B20 (temp only)");
    lfs_reset();
    _mock_time = 1700000100;

    queueData_DS18B20(28.5f);
    TEST("size after 1st entry = 1", getQueueSize() == 1);

    std::string line0 = queue_line(0);
    TEST("format: timestamp,temp",   line0 == "1700000100,28.5");
    TEST("temp value correct",       line0.find("28.5") != std::string::npos);
    TEST("no humidity column",       std::count(line0.begin(),line0.end(),',') == 1);

    _mock_time = 1700000200;
    queueData_DS18B20(30.0f);
    TEST("size after 2nd entry = 2", getQueueSize() == 2);
    TEST("2nd line correct",         queue_line(1) == "1700000200,30.0");
}

void test_queueData_DHT22() {
    SECTION("queueData  DHT22 (temp + humid)");
    lfs_reset();
    _mock_time = 1700000100;

    queueData_DHT22(28.5f, 65.0f);
    TEST("size after 1st entry = 1",  getQueueSize() == 1);

    std::string line0 = queue_line(0);
    TEST("format: timestamp,temp,humid", line0 == "1700000100,28.5,65.0");
    TEST("has 2 commas",              std::count(line0.begin(),line0.end(),',') == 2);

    // temp ติดลบ (cold storage scenario)
    lfs_reset();
    _mock_time = 1700000200;
    queueData_DHT22(-5.5f, 80.0f);
    TEST("negative temp format",      queue_line(0) == "1700000200,-5.5,80.0");
}

void test_queue_overflow() {
    SECTION("Queue Overflow  (MAX=96, oldest entry evicted)");
    lfs_reset();
    _mock_time = 1700000000;

    // เติม 96 entries
    for (int i = 0; i < MAX_QUEUE_ENTRIES; i++) {
        _mock_time = 1700000000 + i;
        queueData_DS18B20(20.0f + i * 0.1f);
    }
    TEST("size at limit = 96",              getQueueSize() == MAX_QUEUE_ENTRIES);
    TEST("oldest entry is entry[0]",        queue_line(0).find("1700000000") != std::string::npos);
    TEST("newest entry is entry[95]",       queue_line(95).find(std::to_string(1700000000+95)) != std::string::npos);

    // เติม entry ที่ 97 → oldest ถูกลบ
    _mock_time = 1700000100;
    queueData_DS18B20(99.9f);
    TEST("size stays at 96 after overflow",    getQueueSize() == MAX_QUEUE_ENTRIES);
    TEST("oldest entry[0] evicted",            queue_line(0).find("1700000000,") == std::string::npos);
    TEST("2nd-oldest becomes entry[0]",        queue_line(0).find("1700000001") != std::string::npos);
    TEST("new entry is entry[95]",             queue_line(95).find("1700000100") != std::string::npos);
    TEST("new entry value = 99.9",             queue_line(95).find("99.9") != std::string::npos);

    // เติมอีก 2 ครั้ง → evict entry[1] และ entry[2]
    _mock_time = 1700000200;
    queueData_DS18B20(88.8f);
    _mock_time = 1700000300;
    queueData_DS18B20(77.7f);
    TEST("size still 96 after 2 more",    getQueueSize() == MAX_QUEUE_ENTRIES);
    TEST("entry[0] is now original [3]",  queue_line(0).find("1700000003") != std::string::npos);
}

void test_queue_timestamp_fallback() {
    SECTION("Queue Timestamp Fallback  (เวลาไม่ sync, ใช้ lastSyncTimeEpoch)");
    lfs_reset();

    // กรณี: time() ยังเป็น 0 แต่เรา sync ไปแล้วครั้งนึง
    _mock_time        = 500;          // ยังไม่ sync (< 1000000000)
    _mock_millis      = 120000;       // millis = 2 นาที
    lastSyncTimeEpoch  = 1700000000;  // เคย sync เมื่อ...
    lastSyncTimeMillis = 60000;       // ...1 นาทีที่แล้ว

    queueData_DS18B20(25.0f);

    std::string line = queue_line(0);
    // estimated time = lastSyncTimeEpoch + (millis - lastSyncTimeMillis)/1000
    //                = 1700000000 + (120000-60000)/1000 = 1700000060
    TEST("timestamp estimated from lastSync", line.find("1700000060") != std::string::npos);

    // กรณี: time() = 0 และไม่เคย sync เลย
    lfs_reset();
    _mock_time         = 500;
    lastSyncTimeEpoch  = 0;
    lastSyncTimeMillis = 0;

    queueData_DS18B20(25.0f);
    line = queue_line(0);
    TEST("no sync ref → raw time()",  line.find("500,") != std::string::npos);

    // reset
    lastSyncTimeEpoch = 0; lastSyncTimeMillis = 0; _mock_time = 1700000000;
}

void test_csv_parsing_DS18B20() {
    SECTION("CSV Parsing  DS18B20 (flushQueue logic)");

    // format ปัจจุบัน: timestamp,temp
    auto e = parseEntry03("1700000000,28.5");
    TEST("valid modern format",        e.valid == true);
    TEST("timestamp parsed",           e.timestamp == "1700000000");
    TEST("temp parsed",                e.temp == "28.5");

    // format เก่า (ก่อน timestamp): temp เฉยๆ ไม่มี comma = invalid ใน parser
    auto e2 = parseEntry03("28.5");
    TEST("no comma → invalid",         e2.valid == false);

    // มี 3 fields (เกิดจาก queue เก่า DHT22 ที่มี humid ด้วย)
    auto e3 = parseEntry03("1700000000,28.5,65.0");
    TEST("3-field: timestamp correct",  e3.timestamp == "1700000000");
    TEST("3-field: temp correct",       e3.temp == "28.5");

    // temp ติดลบ
    auto e4 = parseEntry03("1700000000,-5.5");
    TEST("negative temp parsed",        e4.temp == "-5.5");

    // empty line
    auto e5 = parseEntry03("");
    TEST("empty line → invalid",        e5.valid == false);
}

void test_csv_parsing_DHT22() {
    SECTION("CSV Parsing  DHT22 (flushQueue logic)");

    // format ปัจจุบัน: timestamp,temp,humid
    auto e = parseEntry02("1700000000,28.5,65.0");
    TEST("valid modern format",         e.valid == true);
    TEST("timestamp parsed",            e.timestamp == "1700000000");
    TEST("temp parsed",                 e.temp == "28.5");
    TEST("humid parsed",                e.humid == "65.0");

    // format เก่า (ไม่มี timestamp): temp,humid
    auto e2 = parseEntry02("28.5,65.0");
    TEST("old format (no timestamp)",   e2.valid == true);
    TEST("old format temp",             e2.temp == "28.5");
    TEST("old format humid",            e2.humid == "65.0");
    TEST("old format timestamp empty",  e2.timestamp == "");

    // humid = 100% (boundary)
    auto e3 = parseEntry02("1700000000,35.0,100.0");
    TEST("humid 100% parsed",           e3.humid == "100.0");

    // humid = 0%
    auto e4 = parseEntry02("1700000000,35.0,0.0");
    TEST("humid 0% parsed",             e4.humid == "0.0");

    // temp ติดลบ + humid สูง
    auto e5 = parseEntry02("1700000000,-10.5,95.0");
    TEST("negative temp + high humid",  e5.temp == "-10.5" && e5.humid == "95.0");

    // no comma = invalid
    auto e6 = parseEntry02("28.5");
    TEST("no comma → invalid",          e6.valid == false);
}

void test_queue_partial_flush_simulation() {
    SECTION("Partial Flush Simulation  (entries ที่ส่งสำเร็จถูกลบ, ที่เหลือยังอยู่)");
    lfs_reset();

    // เตรียม 5 entries
    for (int i=0; i<5; i++) {
        _mock_time = 1700000000 + i;
        queueData_DS18B20(20.0f + i);
    }
    TEST("initial size = 5",   getQueueSize() == 5);

    // simulate partial flush: ส่งสำเร็จ 3 จาก 5, rewrite เฉพาะที่เหลือ
    {
        File f = LittleFS.open(QUEUE_FILE, "r");
        std::string entries[5]; int n=0;
        while (f.available() && n<5) {
            String line = f.readStringUntil('\n'); line.trim();
            if (line.length()>2) entries[n++]=line._s;
        }
        f.close();
        int sentCount = 3;
        File fw = LittleFS.open(QUEUE_FILE, "w");
        for (int i=sentCount; i<n; i++) fw.println(String(entries[i].c_str()));
        fw.close();
    }

    TEST("after partial flush size = 2",      getQueueSize() == 2);
    TEST("remaining[0] is original entry[3]", queue_line(0).find("1700000003") != std::string::npos);
    TEST("remaining[1] is original entry[4]", queue_line(1).find("1700000004") != std::string::npos);
}

// ============================================================
// main
// ============================================================
int main() {
    printf("\033[1mTempBot Firmware — Offline Queue Tests  v1.0.9\033[0m\n");
    printf("================================================\n");

    test_getQueueSize();
    test_queueData_DS18B20();
    test_queueData_DHT22();
    test_queue_overflow();
    test_queue_timestamp_fallback();
    test_csv_parsing_DS18B20();
    test_csv_parsing_DHT22();
    test_queue_partial_flush_simulation();

    printf("\n================================================\n");
    if (_failed==0)
        printf("\033[32m✓  ALL %d TESTS PASSED\033[0m\n\n", _passed);
    else
        printf("\033[31m%d passed  /  %d FAILED\033[0m\n\n", _passed, _failed);

    return _failed>0 ? 1 : 0;
}
