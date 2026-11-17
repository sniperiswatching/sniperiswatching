/*
  ChatGPT Usage Monitor - 0.96 inch OLED edition
  Hardware: ESP32-S3 + 0.96 inch SSD1306 128x64 I2C OLED

  The OLED shows the two quota cards and a rotating recent-project page from the supplied reference:
    - 5-hour limit
    - weekly limit
    - three recent projects with completed, working/authorization, or error status

  The usage values come from the local PC bridge. The Edge extension
  forwards only the visible values from the signed-in ChatGPT Usage page.

  Arduino IDE libraries:
    - ArduinoJson 7
    - U8g2
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------------- User configuration ----------------
const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *BRIDGE_URL = "http://192.168.1.100:8765/usage";

// The reference photo labels SCL above SDA. For an I2C OLED they are
// real I2C signals, unlike the ST7789 photo where they were SPI SCK/MOSI.
#define OLED_SDA 11
#define OLED_SCL 12
#define OLED_I2C_ADDRESS 0x3C

const uint32_t USAGE_POLL_MS = 15000;
const uint32_t DRAW_MS = 500;
const uint32_t PAGE_MS = 6000;
const uint32_t WIFI_RECONNECT_MS = 10000;
const uint32_t MAX_AGE_SECONDS = 180;
// ----------------------------------------------------

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

int fiveHourRemaining = -1;
int weeklyRemaining = -1;
String fiveHourReset = "--";
String weeklyReset = "--";
String bridgeStatus = "等待电脑端数据";
uint32_t fetchedAgeSeconds = 999999;
uint32_t fetchedAtMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastDrawMs = 0;
uint32_t lastReconnectMs = 0;
uint32_t pageStartedMs = 0;
bool hasFreshSnapshot = false;
enum DisplayPage : uint8_t {
  USAGE_PAGE = 0,
  RESET_PAGE = 1,
  TASKS_PAGE = 2
};

DisplayPage displayPage = USAGE_PAGE;

struct ProjectItem {
  String title;
  String status;
};

ProjectItem recentProjects[3] = {
  {"暂无项目", "working"},
  {"暂无项目", "working"},
  {"暂无项目", "working"}
};

const uint8_t *fontChinese() {
  return u8g2_font_wqy12_t_gb2312;
}

const uint8_t *fontSmall() {
  return u8g2_font_6x10_tf;
}

uint32_t currentAgeSeconds() {
  if (fetchedAtMs == 0) return 999999;
  return fetchedAgeSeconds + static_cast<uint32_t>((millis() - fetchedAtMs) / 1000UL);
}

String fitUtf8(String value, int maxWidth, const uint8_t *font) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  oled.setFont(font);
  while (value.length() > 0 && oled.getUTF8Width(value.c_str()) > maxWidth) {
    int index = static_cast<int>(value.length()) - 1;
    while (index > 0 && (static_cast<uint8_t>(value[index]) & 0xC0) == 0x80) {
      index--;
    }
    value.remove(index);
  }
  return value;
}

void drawUtf8(int x, int baseline, const String &value, const uint8_t *font) {
  oled.setFont(font);
  oled.drawUTF8(x, baseline, value.c_str());
}

void drawHeader(const char *title) {
  oled.setFont(fontChinese());
  drawUtf8(0, 10, title, fontChinese());
  oled.drawHLine(0, 13, 128);

  const bool connected = WiFi.status() == WL_CONNECTED;
  oled.setFont(fontSmall());
  oled.drawStr(106, 10, connected ? "WiFi" : "OFF");
}

void drawProgressBar(int y, int remaining) {
  oled.drawFrame(0, y, 128, 6);
  if (remaining < 0) return;
  const int fillWidth = constrain(remaining, 0, 100) * 124 / 100;
  if (fillWidth > 0) {
    oled.drawBox(2, y + 2, fillWidth, 2);
  }
}

void drawQuotaRow(int y, const char *label, int remaining) {
  drawUtf8(0, y + 10, label, fontChinese());

  oled.setFont(u8g2_font_helvB14_tf);
  if (remaining >= 0 && remaining <= 100) {
    String percent = String(remaining) + "%";
    const int width = oled.getStrWidth(percent.c_str());
    oled.drawStr(128 - width, y + 12, percent.c_str());
  } else {
    oled.drawStr(101, y + 12, "--%");
  }

  drawProgressBar(y + 15, remaining);
}

void drawStatusLine() {
  String status;
  if (!hasFreshSnapshot) {
    status = bridgeStatus;
  } else {
    status = "实时 " + String(currentAgeSeconds()) + "s";
  }
  drawUtf8(0, 63, fitUtf8(status, 128, fontChinese()), fontChinese());
}

void renderUsagePage() {
  oled.clearBuffer();
  drawHeader("ChatGPT 额度监控");
  drawQuotaRow(15, "5小时限额", fiveHourRemaining);
  drawQuotaRow(38, "每周限额", weeklyRemaining);
  drawStatusLine();
  oled.sendBuffer();
}

void drawResetRow(int y, const char *label, const String &resetText) {
  drawUtf8(0, y, label, fontChinese());
  String compact = fitUtf8(resetText, 76, fontChinese());
  drawUtf8(52, y, compact, fontChinese());
}

void renderResetPage() {
  oled.clearBuffer();
  drawHeader("ChatGPT 重置时间");
  drawResetRow(29, "5小时重置", fiveHourReset);
  drawResetRow(50, "每周重置", weeklyReset);
  drawStatusLine();
  oled.sendBuffer();
}

String projectStatusLabel(const String &status) {
  if (status == "completed") return "已完成";
  if (status == "error") return "出错";
  if (status == "attention") return "待授权";
  return "进行中";
}

void drawProjectMarker(int x, int y, const String &status) {
  if (status == "completed") {
    oled.drawBox(x, y, 7, 7);
    return;
  }

  if (status == "error") {
    oled.drawFrame(x, y, 8, 8);
    oled.drawLine(x, y, x + 7, y + 7);
    oled.drawLine(x + 7, y, x, y + 7);
    return;
  }

  oled.drawFrame(x, y, 8, 8);
}

void drawProjectRow(int y, const ProjectItem &project) {
  oled.drawFrame(0, y, 128, 14);
  drawProjectMarker(2, y + 3, project.status);

  const String title = fitUtf8(project.title, 64, fontChinese());
  drawUtf8(13, y + 11, title, fontChinese());

  const String label = fitUtf8(projectStatusLabel(project.status), 48, fontChinese());
  drawUtf8(78, y + 11, label, fontChinese());
}

void renderProjectsPage() {
  oled.clearBuffer();
  drawHeader("最近3个项目");
  drawProjectRow(15, recentProjects[0]);
  drawProjectRow(30, recentProjects[1]);
  drawProjectRow(45, recentProjects[2]);
  drawStatusLine();
  oled.sendBuffer();
}

void render() {
  if (displayPage == USAGE_PAGE) {
    renderUsagePage();
  } else if (displayPage == RESET_PAGE) {
    renderResetPage();
  } else {
    renderProjectsPage();
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) {
    delay(200);
  }
}

void fetchUsage() {
  if (WiFi.status() != WL_CONNECTED) {
    bridgeStatus = "Wi-Fi 离线";
    hasFreshSnapshot = false;
    return;
  }

  HTTPClient http;
  http.setTimeout(3500);

  if (!http.begin(BRIDGE_URL)) {
    bridgeStatus = "桥接地址错误";
    hasFreshSnapshot = false;
    return;
  }

  const int result = http.GET();
  if (result != HTTP_CODE_OK) {
    bridgeStatus = "电脑桥接离线";
    hasFreshSnapshot = false;
    http.end();
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  if (error) {
    bridgeStatus = "JSON 解析失败";
    hasFreshSnapshot = false;
    http.end();
    return;
  }

  const bool valid = doc["valid"] | false;
  const int five = doc["five_hour_remaining_pct"] | -1;
  const int week = doc["weekly_remaining_pct"] | -1;
  const uint32_t age = doc["age_seconds"] | 999999UL;

  JsonArray projectArray = doc["tasks"].as<JsonArray>();
  if (!projectArray.isNull()) {
    for (int i = 0; i < 3; ++i) {
      if (i < static_cast<int>(projectArray.size())) {
        JsonObject item = projectArray[i].as<JsonObject>();
        recentProjects[i].title = item["title"] | "未命名项目";
        recentProjects[i].status = item["status"] | "working";
        if (recentProjects[i].status != "completed" &&
            recentProjects[i].status != "working" &&
            recentProjects[i].status != "attention" &&
            recentProjects[i].status != "error") {
          recentProjects[i].status = "attention";
        }
      } else {
        recentProjects[i].title = "暂无项目";
        recentProjects[i].status = "working";
      }
    }
  }

  if (!valid || five < 0 || five > 100 || week < 0 || week > 100) {
    fiveHourRemaining = -1;
    weeklyRemaining = -1;
    hasFreshSnapshot = false;
    bridgeStatus = "等待 Usage 数据";
  } else {
    fiveHourRemaining = five;
    weeklyRemaining = week;
    fiveHourReset = doc["five_hour_reset"] | "--";
    weeklyReset = doc["weekly_reset"] | "--";
    fetchedAgeSeconds = age;
    fetchedAtMs = millis();
    hasFreshSnapshot = age <= MAX_AGE_SECONDS;
    bridgeStatus = hasFreshSnapshot ? "实时" : "数据已过期";
  }

  Serial.printf(
    "5-hour=%d%% reset=%s; weekly=%d%% reset=%s; status=%s age=%lu\n",
    fiveHourRemaining,
    fiveHourReset.c_str(),
    weeklyRemaining,
    weeklyReset.c_str(),
    bridgeStatus.c_str(),
    static_cast<unsigned long>(age)
  );
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);

  oled.setI2CAddress(OLED_I2C_ADDRESS << 1);
  oled.begin();
  oled.setContrast(190);
  oled.clearBuffer();
  drawUtf8(0, 11, "ChatGPT OLED", fontChinese());
  drawUtf8(0, 29, "连接 Wi-Fi...", fontChinese());
  oled.sendBuffer();

  connectWifi();
  fetchUsage();

  const uint32_t now = millis();
  lastPollMs = now;
  lastDrawMs = now;
  pageStartedMs = now;
  render();
}

void loop() {
  const uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastReconnectMs >= WIFI_RECONNECT_MS) {
    lastReconnectMs = now;
    WiFi.reconnect();
  }

  if (now - lastPollMs >= USAGE_POLL_MS) {
    lastPollMs = now;
    fetchUsage();
  }

  if (now - pageStartedMs >= PAGE_MS) {
    pageStartedMs = now;
    displayPage = static_cast<DisplayPage>((static_cast<uint8_t>(displayPage) + 1) % 3);
  }

  if (now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    render();
  }

  delay(20);
}

