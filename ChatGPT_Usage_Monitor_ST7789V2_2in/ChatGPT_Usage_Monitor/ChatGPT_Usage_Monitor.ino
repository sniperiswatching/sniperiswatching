/*
  ChatGPT Usage Monitor - ST7789V2 4-SPI edition
  ESP32-S3 N16R8 + 2.0 inch ST7789V2 240x320 TFT

  The layout follows the supplied reference photo:
  - ChatGPT / Work-Codex header
  - 5-hour limit and weekly limit cards
  - three recent project rows colored green, orange, and red by status

  The TFT pin names SCL and SDA are SPI SCK and MOSI on this module.
  They are not I2C SCL/SDA.

  Arduino IDE libraries:
    - Adafruit GFX Library
    - Adafruit ST7735 and ST7789 Library
    - Adafruit BusIO (installed automatically when requested)
    - ArduinoJson 7
    - U8g2_for_Adafruit_GFX
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>

// ---------------- User configuration ----------------
const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *BRIDGE_URL = "http://192.168.1.100:8765/usage";

// Exact mapping from the supplied wiring photo.
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_ROTATION 0       // 0 = portrait 240x320
#define TFT_INVERT false
#define TFT_SCLK 12          // module SCL / SPI SCK
#define TFT_MOSI 11          // module SDA / SPI MOSI
#define TFT_CS 10
#define TFT_DC 13
#define TFT_RST 14

// BLK is module-dependent. Leave it out of the GPIO map and wire it only
// according to the screen seller's specification. The firmware does not drive it.

const uint32_t USAGE_POLL_MS = 15000;
const uint32_t DRAW_MS = 1000;
const uint32_t WIFI_RECONNECT_MS = 10000;
const uint32_t MAX_AGE_SECONDS = 180;
// ----------------------------------------------------

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// RGB565 colors chosen to match the dark neon reference photo.
const uint16_t BG = 0x0008;
const uint16_t CARD = 0x108A;
const uint16_t CARD_ALT = 0x0848;
const uint16_t WHITE = 0xFFFF;
const uint16_t MUTED = 0xB5B6;
const uint16_t CYAN = 0x05FF;
const uint16_t GREEN = 0x07E0;
const uint16_t BLUE = 0x2D9F;
const uint16_t ORANGE = 0xFD20;
const uint16_t RED = 0xF800;
const uint16_t BAR_BG = 0x632C;

struct TaskItem {
  String title;
  String detail;
  String status;
};

TaskItem recentTasks[3] = {
  {"暂无任务", "请在电脑端任务管理页添加任务", "working"},
  {"暂无任务", "等待第二个最近任务", "working"},
  {"暂无任务", "等待第三个最近任务", "working"}
};

int fiveHourRemaining = -1;
int weeklyRemaining = -1;
String fiveHourReset = "--";
String weeklyReset = "--";
String bridgeStatus = "等待数据";
uint32_t fetchedAgeSeconds = 999999;
uint32_t fetchedAtMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastDrawMs = 0;
uint32_t lastReconnectMs = 0;
bool hasFreshSnapshot = false;

uint32_t currentAgeSeconds() {
  if (fetchedAtMs == 0) return 999999;
  return fetchedAgeSeconds + static_cast<uint32_t>((millis() - fetchedAtMs) / 1000UL);
}

void drawChinese(int x, int baseline, const String &value, uint16_t color, const uint8_t *font = u8g2_font_wqy12_t_gb2312) {
  u8g2.setFontMode(1);
  u8g2.setFont(font);
  u8g2.setForegroundColor(color);
  u8g2.drawUTF8(x, baseline, value.c_str());
}

String fitChinese(String value, int maxWidth) {
  value.replace("\r", " ");
  value.replace("\n", " ");
  while (value.length() > 0 && u8g2.getUTF8Width(value.c_str()) > maxWidth) {
    int index = static_cast<int>(value.length()) - 1;
    while (index > 0 && (static_cast<uint8_t>(value[index]) & 0xC0) == 0x80) index--;
    value.remove(index);
  }
  return value;
}

void drawAscii(int x, int y, const String &value, uint16_t color, uint8_t size = 1) {
  tft.setTextWrap(false);
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(value);
}

void drawWifiIcon(int x, int y, bool online) {
  uint16_t color = online ? GREEN : RED;
  tft.drawCircle(x, y + 12, 2, color);
  tft.drawCircle(x, y + 12, 7, color);
  tft.drawCircle(x, y + 12, 12, color);
  tft.fillRect(x - 14, y, 29, 9, BG);
  tft.fillRect(x - 14, y + 9, 4, 9, BG);
  tft.fillRect(x + 10, y + 9, 4, 9, BG);
  tft.fillCircle(x, y + 12, 2, color);
}

void drawProgress(int x, int y, int width, int percentage, uint16_t color) {
  tft.fillRoundRect(x, y, width, 9, 4, BAR_BG);
  if (percentage < 0) return;
  int fillWidth = static_cast<int>((width - 4) * constrain(percentage, 0, 100) / 100.0f);
  if (fillWidth > 0) tft.fillRoundRect(x + 2, y + 2, fillWidth, 5, 3, color);
}

uint16_t taskAccent(const String &status) {
  if (status == "completed") return GREEN;
  if (status == "error") return RED;
  return ORANGE;
}

String taskLabel(const String &status) {
  if (status == "completed") return "已完成";
  if (status == "error") return "出错";
  if (status == "attention") return "待授权";
  return "进行中";
}

String taskIcon(const String &status) {
  if (status == "completed") return "✓";
  if (status == "error") return "!";
  return "…";
}

void drawQuotaCard(int x, const String &title, int remaining, const String &reset, uint16_t color) {
  const int cardWidth = 112;
  const int cardHeight = 91;
  const int y = 43;
  const bool available = hasFreshSnapshot && remaining >= 0 && remaining <= 100;

  tft.fillRoundRect(x, y, cardWidth, cardHeight, 10, CARD);
  tft.drawRoundRect(x, y, cardWidth, cardHeight, 10, color);
  drawChinese(x + 9, y + 19, title, WHITE);

  if (available) {
    drawAscii(x + 10, y + 29, String(remaining) + "%", color, 3);
    drawChinese(x + 72, y + 31, "剩余", color);
  } else {
    drawAscii(x + 10, y + 29, "--%", MUTED, 3);
    drawChinese(x + 72, y + 31, "未同步", MUTED);
  }

  drawProgress(x + 9, y + 53, 94, available ? remaining : -1, color);
  String resetText = available ? ("约 " + fitChinese(reset, 91) + " 后重置") : "等待电脑端数据";
  drawChinese(x + 9, y + 78, fitChinese(resetText, 96), available ? WHITE : MUTED);
}

void drawTaskRow(int y, int index) {
  TaskItem &task = recentTasks[index];
  uint16_t accent = taskAccent(task.status);
  String title = fitChinese(task.title, 112);
  String detail = fitChinese(task.detail, 178);
  String label = taskLabel(task.status);

  tft.fillRoundRect(7, y, 226, 49, 9, CARD_ALT);
  tft.drawRoundRect(7, y, 226, 49, 9, accent);
  tft.fillRoundRect(7, y + 5, 4, 39, 2, accent);

  tft.fillCircle(24, y + 24, 13, accent);
  drawAscii(20, y + 17, String(index + 1), BG, 1);
  drawChinese(43, y + 19, title, WHITE);
  drawChinese(43, y + 39, detail, MUTED);

  tft.fillRoundRect(166, y + 7, 59, 19, 8, accent);
  drawAscii(173, y + 11, taskIcon(task.status), BG, 1);
  drawChinese(185, y + 21, label, BG);
}

void render() {
  bool wifiOnline = WiFi.status() == WL_CONNECTED;
  bool fresh = hasFreshSnapshot && currentAgeSeconds() <= MAX_AGE_SECONDS;

  tft.fillScreen(BG);
  drawAscii(12, 5, "ChatGPT", WHITE, 2);
  drawChinese(12, 30, "Work / Codex 额度监控", CYAN);
  drawWifiIcon(203, 4, wifiOnline);
  drawChinese(218, 24, wifiOnline ? "在线" : "离线", wifiOnline ? GREEN : RED);

  drawQuotaCard(6, "5 小时限额", fiveHourRemaining, fiveHourReset, GREEN);
  drawQuotaCard(122, "每周限额", weeklyRemaining, weeklyReset, BLUE);

  drawChinese(9, 151, "▣  最近 3 个任务", WHITE);
  String updateText = fresh ? ("更新 " + String(currentAgeSeconds()) + "s") : bridgeStatus;
  drawChinese(166, 151, fitChinese(updateText, 66), fresh ? MUTED : ORANGE);

  drawTaskRow(158, 0);
  drawTaskRow(211, 1);
  drawTaskRow(264, 2);

  if (!wifiOnline) {
    drawChinese(9, 316, "Wi-Fi 未连接", RED);
  } else if (!fresh) {
    drawChinese(9, 316, "等待电脑端 Usage 页面", ORANGE);
  } else {
    drawChinese(9, 316, "数据已同步", GREEN);
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    delay(200);
  }
}

void updateTaskList(JsonDocument &doc) {
  JsonArray taskArray = doc["tasks"].as<JsonArray>();
  if (taskArray.isNull()) return;

  for (int i = 0; i < 3; ++i) {
    if (i < static_cast<int>(taskArray.size())) {
      JsonObject item = taskArray[i].as<JsonObject>();
      recentTasks[i].title = String((const char *)(item["title"] | "暂无任务"));
      recentTasks[i].detail = String((const char *)(item["detail"] | ""));
      recentTasks[i].status = String((const char *)(item["status"] | "working"));
      if (recentTasks[i].status != "completed" &&
          recentTasks[i].status != "working" &&
          recentTasks[i].status != "attention" &&
          recentTasks[i].status != "error") {
        recentTasks[i].status = "attention";
      }
    } else {
      recentTasks[i] = {"暂无任务", "请在电脑端任务管理页添加任务", "working"};
    }
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

  int result = http.GET();
  if (result != HTTP_CODE_OK) {
    bridgeStatus = "电脑桥接离线";
    hasFreshSnapshot = false;
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream());
  if (error) {
    bridgeStatus = "JSON 解析失败";
    hasFreshSnapshot = false;
    http.end();
    return;
  }

  updateTaskList(doc);
  bool valid = doc["valid"] | false;
  int five = doc["five_hour_remaining_pct"] | -1;
  int week = doc["weekly_remaining_pct"] | -1;
  uint32_t age = doc["age_seconds"] | 999999UL;

  if (!valid || five < 0 || five > 100 || week < 0 || week > 100) {
    hasFreshSnapshot = false;
    fiveHourRemaining = -1;
    weeklyRemaining = -1;
    bridgeStatus = "等待 Usage 数据";
  } else {
    fiveHourRemaining = five;
    weeklyRemaining = week;
    fiveHourReset = String((const char *)(doc["five_hour_reset"] | "--"));
    weeklyReset = String((const char *)(doc["weekly_reset"] | "--"));
    fetchedAgeSeconds = age;
    fetchedAtMs = millis();
    hasFreshSnapshot = age <= MAX_AGE_SECONDS;
    bridgeStatus = hasFreshSnapshot ? "实时" : "数据已过期";
  }

  Serial.printf("5-hour=%d%% reset=%s; weekly=%d%% reset=%s; status=%s age=%lu\n",
                fiveHourRemaining,
                fiveHourReset.c_str(),
                weeklyRemaining,
                weeklyReset.c_str(),
                bridgeStatus.c_str(),
                static_cast<unsigned long>(age));
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT);
  tft.fillScreen(BG);

  u8g2.begin(tft);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);

  drawAscii(14, 12, "ChatGPT", WHITE, 2);
  drawChinese(14, 39, "正在连接 Wi-Fi…", CYAN);
  connectWifi();
  fetchUsage();
  lastPollMs = millis();
  render();
}

void loop() {
  uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastReconnectMs >= WIFI_RECONNECT_MS) {
    lastReconnectMs = now;
    WiFi.reconnect();
  }

  if (now - lastPollMs >= USAGE_POLL_MS) {
    lastPollMs = now;
    fetchUsage();
  }

  if (now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    render();
  }

  delay(20);
}

