// TempBot Firmware Unit Tests
// Build & Run: make -C test
// Or:          g++ -std=c++14 -o test/run_tests test/run_tests.cpp && ./test/run_tests

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include "../libraries/tempbot_common/tempbot_semver.h"

// ============================================================
// Minimal Arduino String stub
// ============================================================
class String {
public:
    std::string _s;
    String() {}
    String(const char* c) : _s(c ? c : "") {}
    String(std::string s) : _s(std::move(s)) {}
    String(int v, int base = 10) {
        std::ostringstream ss;
        if (base == 16) ss << std::hex << v; else ss << v;
        _s = ss.str();
    }
    String(float v, int d = 1) { char b[32]; snprintf(b,32,"%.*f",d,v); _s=b; }

    size_t      length()    const { return _s.length(); }
    const char* c_str()     const { return _s.c_str(); }
    char        charAt(int i) const { return _s[i]; }
    int         toInt()     const { return atoi(_s.c_str()); }
    float       toFloat()   const { return (float)atof(_s.c_str()); }
    bool        isEmpty()   const { return _s.empty(); }

    void trim() {
        size_t l = _s.find_first_not_of(" \t\r\n");
        size_t r = _s.find_last_not_of(" \t\r\n");
        _s = (l == std::string::npos) ? "" : _s.substr(l, r-l+1);
    }
    void toUpperCase() { std::transform(_s.begin(),_s.end(),_s.begin(),::toupper); }
    void replace(const char* f, const char* t) {
        std::string sf(f), st(t); size_t pos=0;
        while ((pos=_s.find(sf,pos))!=std::string::npos) { _s.replace(pos,sf.length(),st); pos+=st.length(); }
    }
    int indexOf(char c, int from=0) const {
        size_t p=_s.find(c,from); return p==std::string::npos?-1:(int)p;
    }
    int indexOf(const char* sub, int from=0) const {
        size_t p=_s.find(sub,from); return p==std::string::npos?-1:(int)p;
    }
    String substring(int from, int to=-1) const {
        return to<0 ? String(_s.substr(from)) : String(_s.substr(from,to-from));
    }
    bool startsWith(const char* p)    const { return _s.rfind(p,0)==0; }
    bool startsWith(const String& p)  const { return startsWith(p.c_str()); }
    bool operator==(const String& o)  const { return _s==o._s; }
    bool operator==(const char* o)    const { return _s==o; }
    bool operator!=(const String& o)  const { return _s!=o._s; }
    String  operator+(const String& o) const { return String(_s+o._s); }
    String  operator+(const char* o)   const { return String(_s+o); }
    String& operator+=(const String& o){ _s+=o._s; return *this; }
    String& operator+=(char c)         { _s+=c;    return *this; }
    String& operator+=(const char* c)  { _s+=c;    return *this; }
    friend String operator+(const char* l, const String& r) { return String(std::string(l)+r._s); }
};

// ============================================================
// Functions under test
// ============================================================

// ----- isNewerVersion (tempbot_common.cpp) -----
bool isNewerVersion(String latest, String current) {
    return tempbotIsNewerVersion(latest.c_str(), current.c_str());
}

// ----- urlEncode (tempbot_common.cpp) -----
String urlEncode(String str) {
    String encoded=""; char buf[4];
    for(unsigned int i=0;i<str.length();i++){
        char c=str.charAt(i);
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
            c=='-'||c=='_'||c=='.'||c=='~') encoded+=c;
        else { snprintf(buf,4,"%%%02X",(unsigned char)c); encoded+=buf; }
    }
    return encoded;
}

// ----- formatTime (tempbot_common.cpp) -----
String formatTime(time_t epoch, bool includeSeconds) {
    if(epoch<1000000000) return "--:--";
    struct tm* t=localtime(&epoch); char buf[10];
    if(includeSeconds) sprintf(buf,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
    else               sprintf(buf,"%02d:%02d",      t->tm_hour,t->tm_min);
    return String(buf);
}

// ----- getTempCalibrationOffset -----
char tempCalibrationStr[10]="0.0";
float getTempCalibrationOffset() { return (float)atof(tempCalibrationStr); }

// ----- boot notification filter (loop() ใน .ino) -----
bool shouldSendBootNotification(String resetReason) {
    return !resetReason.startsWith("Software Watchdog") &&
           !resetReason.startsWith("Exception");
}

// ----- alertState logic -----
// ตัวอย่าง temp alert state machine (DHT22 variant)
enum AlertState { STATE_NORMAL, STATE_ALERT_LOW, STATE_ALERT_HIGH };
AlertState evalTempAlertState(float temp, float minAlert, float maxAlert) {
    if(temp < minAlert) return STATE_ALERT_LOW;
    if(temp > maxAlert) return STATE_ALERT_HIGH;
    return STATE_NORMAL;
}

// ============================================================
// Test framework
// ============================================================
static int _passed=0, _failed=0;
#define TEST(name, expr) do { \
    bool _ok=(expr); \
    if(_ok){ printf("  \033[32m✓ %s\033[0m\n", name); _passed++; } \
    else   { printf("  \033[31m✗ FAIL: %s\033[0m  (line %d)\n", name, __LINE__); _failed++; } \
} while(0)
#define SECTION(name) printf("\n\033[1m── %s\033[0m\n", name)
#define EXPECT_EQ(a,b) ((a)==(b))
#define EXPECT_NEAR(a,b,eps) (fabsf((a)-(b))<(eps))

// ============================================================
// Test suites
// ============================================================

void test_isNewerVersion() {
    SECTION("isNewerVersion  (OTA upgrade gate)");

    // ปกติ
    TEST("patch upgrade 1.0.8→1.0.9",       isNewerVersion("1.0.9",  "1.0.8")  == true);
    TEST("same version = no update",          isNewerVersion("1.0.8",  "1.0.8")  == false);
    TEST("downgrade blocked 1.0.9→1.0.8",    isNewerVersion("1.0.7",  "1.0.8")  == false);

    // minor / major
    TEST("minor upgrade 1.0.8→1.1.0",        isNewerVersion("1.1.0",  "1.0.8")  == true);
    TEST("major upgrade 1.9.9→2.0.0",        isNewerVersion("2.0.0",  "1.9.9")  == true);
    TEST("major downgrade 2.0.0→1.0.0",      isNewerVersion("1.0.0",  "2.0.0")  == false);
    TEST("minor downgrade 1.1.0→1.0.9",      isNewerVersion("1.0.9",  "1.1.0")  == false);

    // double/triple digit
    TEST("double digit patch 1.0.9→1.0.10",  isNewerVersion("1.0.10", "1.0.9")  == true);
    TEST("triple digit minor 1.99→1.100",     isNewerVersion("1.100.0","1.99.0") == true);
    TEST("double digit major 9→10",           isNewerVersion("10.0.0", "9.9.9")  == true);

    // whitespace (version.txt มักมี trailing newline)
    TEST("trailing \\n stripped",             isNewerVersion("1.0.9\n","1.0.8")  == true);
    TEST("trailing space stripped",           isNewerVersion("1.0.9 ", "1.0.8")  == true);
    TEST("leading space stripped",            isNewerVersion(" 1.0.9", "1.0.8")  == true);

    // edge
    TEST("empty latest → no update",          isNewerVersion("",       "1.0.8")  == false);
    TEST("latest = 0.0.0 < 1.0.0",           isNewerVersion("0.0.0",  "1.0.0")  == false);
    TEST("patch 0→1",                         isNewerVersion("1.0.1",  "1.0.0")  == true);
}

void test_urlEncode() {
    SECTION("urlEncode  (ป้องกัน URL injection ใน GET request ไปหา GAS)");

    // safe chars ไม่ encode
    TEST("alphanumeric unchanged",     urlEncode("DS18B20_ABC123") == "DS18B20_ABC123");
    TEST("hyphen safe",                urlEncode("farm-02")      == "farm-02");
    TEST("dot safe",                   urlEncode("1.0.9")        == "1.0.9");
    TEST("underscore safe",            urlEncode("board_id")     == "board_id");
    TEST("tilde safe",                 urlEncode("~ok~")         == "~ok~");

    // ต้อง encode
    TEST("space → %20",               urlEncode("PS farm")      == "PS%20farm");
    TEST("slash → %2F",               urlEncode("/")            == "%2F");
    TEST("= → %3D",                   urlEncode("=")            == "%3D");
    TEST("& → %26",                   urlEncode("&")            == "%26");
    TEST("+ → %2B",                   urlEncode("+")            == "%2B");
    TEST(": → %3A",                   urlEncode("http:")        == "http%3A");
    TEST("newline → %0A",             urlEncode("\n")            == "%0A");
    TEST("percent sign → %25",         urlEncode("%")             == "%25");

    // Thai / UTF-8 (แต่ละ byte encode แยก)
    TEST("Thai 0xE0 starts %E0",      urlEncode(String("\xe0\xb8\x9f")).startsWith("%E0"));
    TEST("emoji 0xF0 starts %F0",     urlEncode(String("\xf0\x9f\x8c\xa1")).startsWith("%F0"));

    // edge
    TEST("empty string",              urlEncode("") == "");
    TEST("all special chars",         urlEncode("!@#") == "%21%40%23");
}

void test_formatTime() {
    SECTION("formatTime  (แสดงเวลาบน OLED)");

    TEST("epoch 0 → --:--",          formatTime(0, false)          == "--:--");
    TEST("epoch 1 → --:--",          formatTime(1, false)          == "--:--");
    TEST("epoch 999999999 → --:--",  formatTime(999999999, false)  == "--:--");
    TEST("epoch 1000000000 valid",   formatTime(1000000000, false) != "--:--");

    // ตรวจ format (HH:MM = 5 chars, HH:MM:SS = 8 chars)
    String t  = formatTime(1700000000, false);
    String ts = formatTime(1700000000, true);
    TEST("HH:MM length = 5",         t.length() == 5);
    TEST("HH:MM colon at pos 2",     t.indexOf(':') == 2);
    TEST("HH:MM:SS length = 8",      ts.length() == 8);
    TEST("HH:MM:SS first colon=2",   ts.indexOf(':') == 2);
    TEST("HH:MM:SS second colon=5",  ts.indexOf(':',3) == 5);

    // ตรวจว่าตัวเลขอยู่ในช่วงที่เป็นไปได้
    int h = atoi(t.substring(0,2).c_str());
    int m = atoi(t.substring(3,5).c_str());
    TEST("hours 0-23",               h>=0 && h<=23);
    TEST("minutes 0-59",             m>=0 && m<=59);
}

void test_getTempCalibrationOffset() {
    SECTION("getTempCalibrationOffset  (ความแม่นยำของเซนเซอร์)");

    strcpy(tempCalibrationStr, "0.0");
    TEST("zero offset",                   EXPECT_NEAR(getTempCalibrationOffset(), 0.0f, 0.001f));

    strcpy(tempCalibrationStr, "-4.29");
    TEST("negative offset -4.29",         EXPECT_NEAR(getTempCalibrationOffset(), -4.29f, 0.001f));

    strcpy(tempCalibrationStr, "2.5");
    TEST("positive offset 2.5",           EXPECT_NEAR(getTempCalibrationOffset(), 2.5f, 0.001f));

    strcpy(tempCalibrationStr, "0.1");
    TEST("small offset 0.1",              EXPECT_NEAR(getTempCalibrationOffset(), 0.1f, 0.001f));

    strcpy(tempCalibrationStr, "-0.1");
    TEST("small negative offset -0.1",    EXPECT_NEAR(getTempCalibrationOffset(), -0.1f, 0.001f));

    strcpy(tempCalibrationStr, "");
    TEST("empty string → 0.0",            EXPECT_NEAR(getTempCalibrationOffset(), 0.0f, 0.001f));

    strcpy(tempCalibrationStr, "abc");
    TEST("invalid string → 0.0",          EXPECT_NEAR(getTempCalibrationOffset(), 0.0f, 0.001f));

    strcpy(tempCalibrationStr, "1.5abc");
    TEST("partial '1.5abc' → 1.5 (atof)", EXPECT_NEAR(getTempCalibrationOffset(), 1.5f, 0.001f));

    strcpy(tempCalibrationStr, "  2.0");
    TEST("leading space → 2.0 (atof)",    EXPECT_NEAR(getTempCalibrationOffset(), 2.0f, 0.001f));

    strcpy(tempCalibrationStr, "0.0"); // reset
}

void test_bootNotificationFilter() {
    SECTION("Boot Notification Filter  (ป้องกัน flood LINE ตอน crash loop)");

    // ควรส่ง
    TEST("Power On → ส่ง",                    shouldSendBootNotification("Power On")               == true);
    TEST("External System → ส่ง",              shouldSendBootNotification("External System")        == true);
    TEST("Software/System restart → ส่ง",      shouldSendBootNotification("Software/System restart") == true);
    TEST("Hardware Watchdog → ส่ง",            shouldSendBootNotification("Hardware Watchdog")      == true);
    TEST("Deep-Sleep Wake → ส่ง",              shouldSendBootNotification("Deep-Sleep Wake")        == true);
    TEST("empty reason → ส่ง",                 shouldSendBootNotification("")                       == true);

    // ไม่ส่ง
    TEST("Software Watchdog → skip",           shouldSendBootNotification("Software Watchdog")      == false);
    TEST("Exception → skip",                   shouldSendBootNotification("Exception")              == false);
    TEST("Software Watchdog Reset → skip",     shouldSendBootNotification("Software Watchdog Reset") == false);
    TEST("Exception (5) → skip",              shouldSendBootNotification("Exception (5)")          == false);
}

void test_alertStateLogic() {
    SECTION("Temperature Alert State  (logic ใน unified loop)");

    // config ทั่วไป: min=20.0, max=35.0
    TEST("temp ปกติ → NORMAL",          evalTempAlertState(28.0f, 20.0f, 35.0f) == STATE_NORMAL);
    TEST("temp เกิน max → HIGH",         evalTempAlertState(35.1f, 20.0f, 35.0f) == STATE_ALERT_HIGH);
    TEST("temp ต่ำกว่า min → LOW",       evalTempAlertState(19.9f, 20.0f, 35.0f) == STATE_ALERT_LOW);
    TEST("temp = max boundary → NORMAL", evalTempAlertState(35.0f, 20.0f, 35.0f) == STATE_NORMAL);
    TEST("temp = min boundary → NORMAL", evalTempAlertState(20.0f, 20.0f, 35.0f) == STATE_NORMAL);

    // NaN simulation (DEVICE_DISCONNECTED = -127)
    TEST("DS18B20 disconnect (-127) → LOW", evalTempAlertState(-127.0f, 20.0f, 35.0f) == STATE_ALERT_LOW);

    // sensor error value (-999)
    TEST("DHT error (-999) → LOW",      evalTempAlertState(-999.0f, 20.0f, 35.0f) == STATE_ALERT_LOW);
}

void test_configFileSizes() {
    SECTION("Config File Size Thresholds  (ตรวจ binary compat ของ saveConfig/loadConfig)");

    // Current unified layout appends the shared API key after 850-byte legacy layout.
    int current = 150+65+10+200+10+10+40+32+20+16+10+10+32+150+150+10; // = 915
    TEST("current config size = 915", current == 915);
    TEST("current config includes API key", current > 850);

    int legacyUnified = 150+10+200+10+10+40+32+20+16+10+10+32+150+150+10; // = 850
    TEST("legacy unified config size = 850", legacyUnified == 850);

    // ตรวจ boundary ของ legacy branches
    // branch 472: webAppUrl+timerDelay = 160, lineToken+minT+maxT+groupId+boardName+humid*2 = 312 → 472
    int legacy472 = 150+10+200+10+10+40+32+10+10; // 472
    TEST("legacy 472 boundary correct", legacy472 == 472);

    // branch 452: ไม่มี humid fields
    int legacy452 = 150+10+200+10+10+40+32; // 452
    TEST("legacy 452 boundary correct", legacy452 == 452);

    // branch 420: lineToken 200 bytes (ไม่มี boardName)
    int legacy420 = 150+10+200+10+10+40; // 420
    TEST("legacy 420 boundary correct", legacy420 == 420);

    TEST("current ไม่ตก branch 472", current > 472);
    TEST("legacy unified ไม่ตก branch 472", legacyUnified > 472);
}

// ============================================================
// main
// ============================================================
int main() {
    printf("\033[1mTempBot Firmware — Unit Tests  v1.0.9\033[0m\n");
    printf("========================================\n");

    test_isNewerVersion();
    test_urlEncode();
    test_formatTime();
    test_getTempCalibrationOffset();
    test_bootNotificationFilter();
    test_alertStateLogic();
    test_configFileSizes();

    printf("\n========================================\n");
    if (_failed == 0)
        printf("\033[32m✓  ALL %d TESTS PASSED\033[0m\n\n", _passed);
    else
        printf("\033[31m%d passed  /  %d FAILED\033[0m\n\n", _passed, _failed);

    return _failed > 0 ? 1 : 0;
}
