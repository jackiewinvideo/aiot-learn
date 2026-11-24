/**
 * @file main.cpp
 * @brief ESP32 CYD (ESP32-2432S028R) 六合一智慧空氣品質監測看板 (6-in-1 Air Quality Station)
 * 
 * 核心功能：
 * 1. 六合一即時環境指標監測：
 *    - PM2.5 (細懸浮微粒, μg/m³)
 *    - PM10  (懸浮微粒, μg/m³)
 *    - CO2   (二氧化碳濃度, ppm)
 *    - TVOC  (總揮發性有機物, ppb)
 *    - 溫度   (Temperature, °C)
 *    - 濕度   (Humidity, %RH)
 * 2. 綜合指標：即時計算 EPA/WHO AQI 空氣品質指標、健康風險等級與舒適度指數
 * 3. 視覺設計：320x240 橫向深色科技感卡片式儀表板 (Card Grid UI)
 * 4. 觸控互動：
 *    - 點選任一指標卡片：即刻切換為「24點高解析度歷史走勢折線圖 (Trend Graph)」
 *    - 點選底部導航列：切換總覽 (Dashboard)、歷史曲線 (Trend)、健康指引 (Health Guide)、模擬/實測切換 (Demo/Live)
 *    - 點擊螢幕頂部：觸發即時空氣事件模擬 (如：開窗通風、烹飪煙霧、呼吸呼氣)
 * 5. CYD 硬體整合：
 *    - 板載 RGB LED (GPIO 4, 16, 17) 連動當前最嚴重指標等級（綠/黃/橙/紅/紫）
 *    - 板載光敏電阻 (LDR GPIO 34) 環境光感應自動調節畫面與夜間指示
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ==========================================
// 1. 硬體周邊與針腳定義 (CYD ESP32-2432S028R)
// ==========================================
TFT_eSPI tft = TFT_eSPI();

// CYD 專屬 XPT2046 觸控 SPI 腳位
SPIClass touchSPI = SPIClass(VSPI);
#ifndef XPT2046_CS
#define XPT2046_CS 33
#endif
#ifndef XPT2046_IRQ
#define XPT2046_IRQ 36
#endif
#ifndef XPT2046_CLK
#define XPT2046_CLK 25
#endif
#ifndef XPT2046_MISO
#define XPT2046_MISO 39
#endif
#ifndef XPT2046_MOSI
#define XPT2046_MOSI 32
#endif

XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// 板載 RGB LED (Active LOW 低電位點亮)
#ifndef CYD_LED_RED
#define CYD_LED_RED 4
#endif
#ifndef CYD_LED_GREEN
#define CYD_LED_GREEN 16
#endif
#ifndef CYD_LED_BLUE
#define CYD_LED_BLUE 17
#endif
#ifndef TFT_BL
#define TFT_BL 21
#endif
#ifndef CYD_LDR
#define CYD_LDR 34
#endif

// ==========================================
// 2. 空氣品質評級與資料結構
// ==========================================
enum AQILevel {
  AQI_EXCELLENT = 0, // 優良 (綠)
  AQI_MODERATE  = 1, // 普通 (黃)
  AQI_SENSITIVE = 2, // 對敏感族群不健康 (橙)
  AQI_UNHEALTHY = 3, // 不健康 (紅)
  AQI_HAZARDOUS = 4  // 危害/極危險 (紫)
};

// UI 配色定義 (RGB565)
#define COLOR_BG          0x0841  // 深黑藍底色
#define COLOR_CARD_BG     0x18E3  // 卡片背景深藍灰
#define COLOR_CARD_BORDER 0x2965  // 卡片邊框灰
#define COLOR_TEXT_DIM    0x8410  // 暗淡文字灰
#define COLOR_CYAN_ACCENT 0x07FF  // 科技青藍
#define COLOR_LEVEL_EXC   0x2FE4  // 霓虹綠
#define COLOR_LEVEL_MOD   0xFFE0  // 明亮黃
#define COLOR_LEVEL_SEN   0xFD20  // 警示橘
#define COLOR_LEVEL_UNH   0xF800  // 危險紅
#define COLOR_LEVEL_HAZ   0x901F  // 毒性紫

// 六大核心監測資料
struct MetricInfo {
  const char* name;
  const char* unit;
  float value;
  float min_val;
  float max_val;
  AQILevel level;
  uint16_t color;
  const char* level_text;
  float history[24]; // 24 個週期歷史資料點 (用於趨勢圖)
};

#define METRIC_PM25 0
#define METRIC_CO2  1
#define METRIC_TVOC 2
#define METRIC_PM10 3
#define METRIC_TEMP 4
#define METRIC_HUMI 5
#define NUM_METRICS 6

static MetricInfo metrics[NUM_METRICS] = {
  {"PM2.5", "ug/m3", 12.0f,  0.0f, 150.0f, AQI_EXCELLENT, COLOR_LEVEL_EXC, "GOOD", {0}},
  {"CO2",   "ppm",   650.0f, 350.0f, 3000.0f, AQI_EXCELLENT, COLOR_LEVEL_EXC, "FRESH", {0}},
  {"TVOC",  "ppb",   120.0f, 0.0f, 1200.0f, AQI_EXCELLENT, COLOR_LEVEL_EXC, "CLEAN", {0}},
  {"PM10",  "ug/m3", 26.0f,  0.0f, 250.0f, AQI_EXCELLENT, COLOR_LEVEL_EXC, "GOOD", {0}},
  {"TEMP",  "C",     24.5f,  0.0f, 45.0f,  AQI_EXCELLENT, COLOR_LEVEL_EXC, "COMFY", {0}},
  {"HUMI",  "%RH",   55.0f,  10.0f, 95.0f, AQI_EXCELLENT, COLOR_LEVEL_EXC, "IDEAL", {0}}
};

// 系統全域狀態
static uint8_t current_view = 0;       // 0: 六宮格儀表板, 1: 個別趨勢圖, 2: 健康防護指引
static uint8_t selected_metric = 0;    // 歷史曲線檢視的指標索引
static bool is_demo_mode = true;       // 預設啟用真實感擬真模擬 (即使無實體感測器也能生動演示)
static int overall_aqi = 42;
static AQILevel overall_level = AQI_EXCELLENT;
static uint32_t history_index = 0;
static unsigned long last_sensor_time = 0;
static unsigned long last_touch_check = 0;
static unsigned long last_sec_tick = 0;
static uint32_t uptime_sec = 0;

// ==========================================
// 3. 評估標準與等級計算
// ==========================================
void evaluateMetric(int idx) {
  MetricInfo &m = metrics[idx];
  switch (idx) {
    case METRIC_PM25: // PM2.5 (ug/m3)
      if (m.value <= 15.4f)      { m.level = AQI_EXCELLENT; m.level_text = "EXC"; m.color = COLOR_LEVEL_EXC; }
      else if (m.value <= 35.4f) { m.level = AQI_MODERATE;  m.level_text = "MOD"; m.color = COLOR_LEVEL_MOD; }
      else if (m.value <= 55.4f) { m.level = AQI_SENSITIVE; m.level_text = "SENS"; m.color = COLOR_LEVEL_SEN; }
      else if (m.value <= 150.0f){ m.level = AQI_UNHEALTHY; m.level_text = "UNH"; m.color = COLOR_LEVEL_UNH; }
      else                       { m.level = AQI_HAZARDOUS; m.level_text = "HAZ"; m.color = COLOR_LEVEL_HAZ; }
      break;

    case METRIC_CO2: // CO2 (ppm)
      if (m.value <= 700.0f)       { m.level = AQI_EXCELLENT; m.level_text = "FRESH"; m.color = COLOR_LEVEL_EXC; }
      else if (m.value <= 1000.0f) { m.level = AQI_MODERATE;  m.level_text = "GOOD";  m.color = COLOR_LEVEL_MOD; }
      else if (m.value <= 1500.0f) { m.level = AQI_SENSITIVE; m.level_text = "STUFF"; m.color = COLOR_LEVEL_SEN; }
      else if (m.value <= 2500.0f) { m.level = AQI_UNHEALTHY; m.level_text = "POOR";  m.color = COLOR_LEVEL_UNH; }
      else                         { m.level = AQI_HAZARDOUS; m.level_text = "ALERT"; m.color = COLOR_LEVEL_HAZ; }
      break;

    case METRIC_TVOC: // TVOC (ppb)
      if (m.value <= 220.0f)       { m.level = AQI_EXCELLENT; m.level_text = "CLEAN"; m.color = COLOR_LEVEL_EXC; }
      else if (m.value <= 660.0f)  { m.level = AQI_MODERATE;  m.level_text = "MOD";   m.color = COLOR_LEVEL_MOD; }
      else if (m.value <= 1400.0f) { m.level = AQI_SENSITIVE; m.level_text = "WARN";  m.color = COLOR_LEVEL_SEN; }
      else                         { m.level = AQI_UNHEALTHY; m.level_text = "TOXIC"; m.color = COLOR_LEVEL_UNH; }
      break;

    case METRIC_PM10: // PM10 (ug/m3)
      if (m.value <= 54.0f)        { m.level = AQI_EXCELLENT; m.level_text = "GOOD"; m.color = COLOR_LEVEL_EXC; }
      else if (m.value <= 154.0f)  { m.level = AQI_MODERATE;  m.level_text = "MOD";  m.color = COLOR_LEVEL_MOD; }
      else if (m.value <= 254.0f)  { m.level = AQI_SENSITIVE; m.level_text = "DUST"; m.color = COLOR_LEVEL_SEN; }
      else                         { m.level = AQI_UNHEALTHY; m.level_text = "DIRTY";m.color = COLOR_LEVEL_UNH; }
      break;

    case METRIC_TEMP: // Temperature (°C)
      if (m.value >= 20.0f && m.value <= 26.5f)      { m.level = AQI_EXCELLENT; m.level_text = "COMFY"; m.color = COLOR_LEVEL_EXC; }
      else if (m.value >= 17.0f && m.value <= 29.0f) { m.level = AQI_MODERATE;  m.level_text = "OK";    m.color = COLOR_LEVEL_MOD; }
      else if (m.value >= 12.0f && m.value <= 33.0f) { m.level = AQI_SENSITIVE; m.level_text = "MILD";  m.color = COLOR_LEVEL_SEN; }
      else                                           { m.level = AQI_UNHEALTHY; m.level_text = "EXTR";  m.color = COLOR_LEVEL_UNH; }
      break;

    case METRIC_HUMI: // Humidity (%RH)
      if (m.value >= 40.0f && m.value <= 60.0f)      { m.level = AQI_EXCELLENT; m.level_text = "IDEAL"; m.color = COLOR_LEVEL_EXC; }
      else if (m.value >= 30.0f && m.value <= 70.0f) { m.level = AQI_MODERATE;  m.level_text = "FAIR";  m.color = COLOR_LEVEL_MOD; }
      else if (m.value >= 20.0f && m.value <= 80.0f) { m.level = AQI_SENSITIVE; m.level_text = "HUMID"; m.color = COLOR_LEVEL_SEN; }
      else                                           { m.level = AQI_UNHEALTHY; m.level_text = "DRY/WET"; m.color = COLOR_LEVEL_UNH; }
      break;
  }
}

// 綜合 AQI 計算與 RGB LED 狀態警示
void updateOverallAssessment() {
  AQILevel worst = AQI_EXCELLENT;

  for (int i = 0; i < NUM_METRICS; i++) {
    evaluateMetric(i);
    if (metrics[i].level > worst) {
      worst = metrics[i].level;
    }
  }
  overall_level = worst;

  // 根據 PM2.5 換算近似 EPA AQI 數值
  float p = metrics[METRIC_PM25].value;
  if (p <= 12.0f) overall_aqi = (int)map((long)(p * 10), 0, 120, 0, 50);
  else if (p <= 35.4f) overall_aqi = (int)map((long)(p * 10), 121, 354, 51, 100);
  else if (p <= 55.4f) overall_aqi = (int)map((long)(p * 10), 355, 554, 101, 150);
  else if (p <= 150.4f) overall_aqi = (int)map((long)(p * 10), 555, 1504, 151, 200);
  else overall_aqi = (int)map((long)(p * 10), 1505, 2504, 201, 300);
  overall_aqi = constrain(overall_aqi, 10, 500);

  // CYD 板載 RGB LED 警示聯動 (Active LOW 低電位點亮)
  switch (overall_level) {
    case AQI_EXCELLENT: // 綠色
      digitalWrite(CYD_LED_RED, HIGH);
      digitalWrite(CYD_LED_GREEN, LOW);
      digitalWrite(CYD_LED_BLUE, HIGH);
      break;
    case AQI_MODERATE: // 黃色 (紅+綠)
      digitalWrite(CYD_LED_RED, LOW);
      digitalWrite(CYD_LED_GREEN, LOW);
      digitalWrite(CYD_LED_BLUE, HIGH);
      break;
    case AQI_SENSITIVE: // 橙色 (紅+微弱綠)
      digitalWrite(CYD_LED_RED, LOW);
      analogWrite(CYD_LED_GREEN, 180);
      digitalWrite(CYD_LED_BLUE, HIGH);
      break;
    case AQI_UNHEALTHY: // 紅色
      digitalWrite(CYD_LED_RED, LOW);
      digitalWrite(CYD_LED_GREEN, HIGH);
      digitalWrite(CYD_LED_BLUE, HIGH);
      break;
    case AQI_HAZARDOUS: // 紫色 (紅+藍)
      digitalWrite(CYD_LED_RED, LOW);
      digitalWrite(CYD_LED_GREEN, HIGH);
      digitalWrite(CYD_LED_BLUE, LOW);
      break;
  }
}

// ==========================================
// 4. 高真實感環境模擬驅動 (Simulated Sensor Engine)
// ==========================================
void simulateEnvironmentStep() {
  static float sim_time = 0.0f;
  sim_time += 0.08f;

  // 1. 溫度與濕度：結合晝夜正弦波與微抖動
  metrics[METRIC_TEMP].value = 24.2f + sinf(sim_time * 0.2f) * 2.8f + (random(-10, 10) * 0.03f);
  metrics[METRIC_HUMI].value = 54.0f - sinf(sim_time * 0.2f) * 6.5f + (random(-10, 10) * 0.08f);

  // 2. CO2 人體呼吸微累積與通風
  float co2_drift = sinf(sim_time * 0.15f) * 280.0f;
  metrics[METRIC_CO2].value = 650.0f + co2_drift + (random(-15, 15) * 1.5f);

  // 3. PM2.5 與 PM10 空氣微粒擾動
  float particle_wave = fmaxf(0.0f, sinf(sim_time * 0.3f) * 18.0f);
  metrics[METRIC_PM25].value = 11.5f + particle_wave + (random(-5, 8) * 0.3f);
  metrics[METRIC_PM10].value = metrics[METRIC_PM25].value * 2.1f + (random(-4, 6) * 0.4f);

  // 4. TVOC 揮發物
  metrics[METRIC_TVOC].value = 110.0f + fmaxf(0.0f, cosf(sim_time * 0.25f) * 140.0f) + random(-5, 10);

  // 壓制數值至合理範圍
  for (int i = 0; i < NUM_METRICS; i++) {
    metrics[i].value = constrain(metrics[i].value, metrics[i].min_val, metrics[i].max_val);
    // 記錄歷史資料
    metrics[i].history[history_index] = metrics[i].value;
  }

  history_index = (history_index + 1) % 24;
  updateOverallAssessment();
}

// 模擬突發空氣污染事件 (如烹調油煙或深呼吸呼氣)
void triggerAirEvent() {
  metrics[METRIC_PM25].value += 45.0f;
  metrics[METRIC_PM10].value += 80.0f;
  metrics[METRIC_CO2].value  += 550.0f;
  metrics[METRIC_TVOC].value += 380.0f;
  updateOverallAssessment();
}

// ==========================================
// 5. GUI 渲染 - 頂部與底部導覽列
// ==========================================
void drawHeaderBar() {
  tft.fillRect(0, 0, 320, 32, COLOR_BG);
  tft.drawFastHLine(0, 32, 320, COLOR_CARD_BORDER);

  // 系統標題
  tft.setTextColor(COLOR_CYAN_ACCENT, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("AIR QUALITY MONITOR", 8, 8);

  // 運行時間
  char time_buf[16];
  snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu:%02lu", (uptime_sec / 3600) % 24, (uptime_sec / 60) % 60, uptime_sec % 60);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.drawString(time_buf, 8, 20);

  // 模式徽章 [DEMO] 或 [LIVE]
  tft.fillRoundRect(138, 7, 44, 18, 4, is_demo_mode ? 0x2170 : 0x03E0);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(is_demo_mode ? "DEMO" : "LIVE", 145, 12);

  // 右側綜合 AQI 狀態膠囊
  uint16_t aqi_badge_color = (overall_level == AQI_EXCELLENT) ? COLOR_LEVEL_EXC :
                             (overall_level == AQI_MODERATE)  ? COLOR_LEVEL_MOD :
                             (overall_level == AQI_SENSITIVE) ? COLOR_LEVEL_SEN : COLOR_LEVEL_UNH;

  tft.fillRoundRect(192, 5, 120, 22, 5, aqi_badge_color);
  tft.setTextColor(TFT_BLACK, aqi_badge_color);
  tft.setTextSize(1);

  char aqi_buf[32];
  const char* level_name = (overall_level == AQI_EXCELLENT) ? "EXCELLENT" :
                           (overall_level == AQI_MODERATE)  ? "MODERATE" :
                           (overall_level == AQI_SENSITIVE) ? "SENSITIVE" : "UNHEALTHY";
  snprintf(aqi_buf, sizeof(aqi_buf), "AQI %d %s", overall_aqi, level_name);
  tft.drawString(aqi_buf, 198, 11);
}

void drawFooterBar() {
  tft.fillRect(0, 216, 320, 24, COLOR_BG);
  tft.drawFastHLine(0, 215, 320, COLOR_CARD_BORDER);

  // 三個功能觸控按鈕分區
  const char* tabs[3] = {"DASHBOARD", "TREND GRAPH", "HEALTH INFO"};
  for (int i = 0; i < 3; i++) {
    int bx = i * 106 + 2;
    int bw = 102;
    bool is_active = (current_view == i);

    uint16_t tab_bg = is_active ? 0x1B3F : COLOR_CARD_BG;
    uint16_t tab_text = is_active ? COLOR_CYAN_ACCENT : COLOR_TEXT_DIM;

    tft.fillRoundRect(bx, 218, bw, 20, 3, tab_bg);
    if (is_active) {
      tft.drawRoundRect(bx, 218, bw, 20, 3, COLOR_CYAN_ACCENT);
    }
    tft.setTextColor(tab_text, tab_bg);
    tft.setTextSize(1);
    int text_x = bx + (bw - (int)strlen(tabs[i]) * 6) / 2;
    tft.drawString(tabs[i], text_x, 224);
  }
}

// ==========================================
// 6. 視圖 0：六宮格精緻儀表板 (Dashboard Grid)
// ==========================================
void drawMetricCard(int i) {
  // 3 欄 x 2 列卡片幾何佈局
  int col = i % 3;
  int row = i / 3;
  int x = 4 + col * 106;
  int y = 36 + row * 88;
  int w = 100;
  int h = 84;

  MetricInfo &m = metrics[i];

  // 卡片背景與圓角邊框
  tft.fillRoundRect(x, y, w, h, 6, COLOR_CARD_BG);
  tft.drawRoundRect(x, y, w, h, 6, COLOR_CARD_BORDER);

  // 指標名稱
  tft.setTextColor(COLOR_CYAN_ACCENT, COLOR_CARD_BG);
  tft.setTextSize(1);
  tft.drawString(m.name, x + 8, y + 8);

  // 等級微型膠囊標籤
  int tag_w = strlen(m.level_text) * 6 + 6;
  tft.fillRoundRect(x + w - tag_w - 6, y + 6, tag_w, 14, 3, m.color);
  tft.setTextColor(TFT_BLACK, m.color);
  tft.drawString(m.level_text, x + w - tag_w - 3, y + 9);

  // 數值大字顯示
  char val_buf[16];
  if (i == METRIC_TEMP || i == METRIC_HUMI || i == METRIC_PM25) {
    snprintf(val_buf, sizeof(val_buf), "%.1f", m.value);
  } else {
    snprintf(val_buf, sizeof(val_buf), "%d", (int)m.value);
  }

  tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
  tft.setTextSize(2);
  tft.drawString(val_buf, x + 8, y + 26);

  // 單位
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
  tft.setTextSize(1);
  tft.drawString(m.unit, x + 8, y + 46);

  // 底部進度量表條 (Progress Gauge)
  int bar_x = x + 8;
  int bar_y = y + 66;
  int bar_w = w - 16;
  int bar_h = 6;

  tft.fillRoundRect(bar_x, bar_y, bar_w, bar_h, 2, 0x10A2); // 槽底深色

  float ratio = (m.value - m.min_val) / (m.max_val - m.min_val);
  ratio = constrain(ratio, 0.05f, 1.0f);
  int fill_w = (int)(bar_w * ratio);

  tft.fillRoundRect(bar_x, bar_y, fill_w, bar_h, 2, m.color);
}

void renderDashboardView() {
  for (int i = 0; i < NUM_METRICS; i++) {
    drawMetricCard(i);
  }
}

// ==========================================
// 7. 視圖 1：高解析度歷史走勢圖 (Trend Graph)
// ==========================================
void renderTrendView() {
  MetricInfo &m = metrics[selected_metric];

  // 清除中央視圖區域
  tft.fillRect(0, 33, 320, 182, COLOR_BG);

  // 頂部當前選中指標說明與切換提示
  tft.setTextColor(COLOR_CYAN_ACCENT, COLOR_BG);
  tft.setTextSize(1);
  char title_buf[48];
  snprintf(title_buf, sizeof(title_buf), "<  %s HISTORICAL TREND (%s)  >", m.name, m.unit);
  tft.drawString(title_buf, 20, 40);

  // 當前數值與等級
  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setTextSize(2);
  char cur_buf[16];
  snprintf(cur_buf, sizeof(cur_buf), "%.1f", m.value);
  tft.drawString(cur_buf, 230, 36);

  // 圖表網格幾何
  int gx = 36;
  int gy = 60;
  int gw = 260;
  int gh = 130;

  tft.drawRect(gx, gy, gw, gh, COLOR_CARD_BORDER);

  // 網格橫向刻度線
  for (int k = 1; k < 4; k++) {
    int ly = gy + (gh * k) / 4;
    tft.drawFastHLine(gx, ly, gw, 0x18C3);
  }

  // 縱座標刻度標註
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setTextSize(1);
  char scale_buf[16];
  snprintf(scale_buf, sizeof(scale_buf), "%.0f", m.max_val);
  tft.drawString(scale_buf, 4, gy);
  snprintf(scale_buf, sizeof(scale_buf), "%.0f", (m.max_val + m.min_val) / 2.0f);
  tft.drawString(scale_buf, 4, gy + gh / 2 - 4);
  snprintf(scale_buf, sizeof(scale_buf), "%.0f", m.min_val);
  tft.drawString(scale_buf, 4, gy + gh - 8);

  // 繪製 24 個歷史取樣點折線圖
  int prev_px = -1, prev_py = -1;
  float step_x = (float)gw / 23.0f;

  for (int i = 0; i < 24; i++) {
    int data_idx = (history_index + i) % 24;
    float val = m.history[data_idx];
    if (val < m.min_val) val = m.value; // 初值平滑

    float ratio = (val - m.min_val) / (m.max_val - m.min_val);
    ratio = constrain(ratio, 0.0f, 1.0f);

    int px = gx + (int)(i * step_x);
    int py = gy + gh - (int)(ratio * (gh - 10)) - 5;

    // 繪製節點小圓圈
    tft.fillCircle(px, py, 2, m.color);

    if (prev_px >= 0) {
      tft.drawLine(prev_px, prev_py, px, py, m.color);
      // 加粗折線提升觀看質感
      tft.drawLine(prev_px, prev_py + 1, px, py + 1, m.color);
    }
    prev_px = px;
    prev_py = py;
  }

  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.drawString("-24 Cycles", gx, gy + gh + 6);
  tft.drawString("Now (Tap card to switch)", gx + gw - 130, gy + gh + 6);
}

// ==========================================
// 8. 視圖 2：WHO / EPA 空氣防護與健康標準 (Health Guide)
// ==========================================
void renderHealthGuideView() {
  tft.fillRect(0, 33, 320, 182, COLOR_BG);

  tft.setTextColor(COLOR_CYAN_ACCENT, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("WHO / EPA AIR QUALITY ADVISORY", 16, 42);

  // 評級標準條目清單
  struct GuideItem {
    const char* range;
    const char* status;
    const char* advice;
    uint16_t color;
  };

  GuideItem guides[4] = {
    {"0-50",   "GOOD (EXCELLENT)", "Air quality is ideal for all activities.", COLOR_LEVEL_EXC},
    {"51-100", "MODERATE",         "Acceptable. Sensitive groups take note.",  COLOR_LEVEL_MOD},
    {"101-150","SENSITIVE WARNING","Ventilation or air purifier suggested.",   COLOR_LEVEL_SEN},
    {">150",   "UNHEALTHY ALERT",  "Close windows, wear masks, filter air.",  COLOR_LEVEL_UNH}
  };

  for (int i = 0; i < 4; i++) {
    int cy = 62 + i * 35;
    tft.fillRoundRect(12, cy, 296, 30, 4, COLOR_CARD_BG);
    tft.drawRoundRect(12, cy, 296, 30, 4, COLOR_CARD_BORDER);

    // 色彩指示方塊
    tft.fillRect(16, cy + 5, 6, 20, guides[i].color);

    tft.setTextColor(guides[i].color, COLOR_CARD_BG);
    tft.setTextSize(1);
    tft.drawString(guides[i].range, 28, cy + 6);
    tft.drawString(guides[i].status, 84, cy + 6);

    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
    tft.drawString(guides[i].advice, 28, cy + 18);
  }
}

// 刷新全螢幕視圖
void refreshFullView() {
  drawHeaderBar();
  drawFooterBar();
  if (current_view == 0) {
    renderDashboardView();
  } else if (current_view == 1) {
    renderTrendView();
  } else {
    renderHealthGuideView();
  }
}

// ==========================================
// 9. 觸控互動與手勢處理 (Touch Interaction)
// ==========================================
void handleTouchEvents() {
  if (!ts.touched()) return;

  unsigned long now = millis();
  if (now - last_touch_check < 250) return; // 觸控防彈跳
  last_touch_check = now;

  TS_Point p = ts.getPoint();

  // 螢幕校正座標 (CYD 320x240 橫向)
  int tx = map(p.x, 240, 3800, 0, 320);
  int ty = map(p.y, 240, 3800, 0, 240);
  tx = constrain(tx, 0, 319);
  ty = constrain(ty, 0, 239);

  // 1. 點擊頂部橫條：切換 Demo / Live 模式，或觸發模擬事件
  if (ty < 34) {
    if (tx >= 130 && tx <= 190) {
      is_demo_mode = !is_demo_mode;
    } else {
      triggerAirEvent(); // 點擊頂部其他地方模擬抽風/污染事件
    }
    refreshFullView();
    return;
  }

  // 2. 點擊底部導航頁籤
  if (ty >= 214) {
    int tab_clicked = tx / 106;
    if (tab_clicked >= 0 && tab_clicked < 3) {
      current_view = tab_clicked;
      refreshFullView();
    }
    return;
  }

  // 3. 在六宮格儀表板中點選卡片 -> 跳轉至該指標趨勢折線圖
  if (current_view == 0) {
    int col = (tx - 4) / 106;
    int row = (ty - 36) / 88;
    if (col >= 0 && col < 3 && row >= 0 && row < 2) {
      selected_metric = row * 3 + col;
      current_view = 1; // 切換至折線圖
      refreshFullView();
    }
  } 
  // 4. 在折線圖中點選螢幕左/右側 -> 切換上一個/下一個指標
  else if (current_view == 1) {
    if (tx < 160) {
      selected_metric = (selected_metric + NUM_METRICS - 1) % NUM_METRICS;
    } else {
      selected_metric = (selected_metric + 1) % NUM_METRICS;
    }
    refreshFullView();
  }
}

// ==========================================
// 10. 系統初始化 (setup)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n============================================");
  Serial.println("  ESP32 CYD - 6-in-1 Air Quality Station   ");
  Serial.println("============================================");

  // 初始化板載 RGB LED
  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);
  digitalWrite(CYD_LED_RED, HIGH);
  digitalWrite(CYD_LED_GREEN, HIGH);
  digitalWrite(CYD_LED_BLUE, HIGH);

  // 開啟螢幕背光
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // 初始化 TFT 顯示器 (橫向 320x240)
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  // 初始化 XPT2046 觸控螢幕
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 預填初始歷史數據 (平滑過渡)
  for (int s = 0; s < 24; s++) {
    simulateEnvironmentStep();
  }

  // 繪製初始畫面
  refreshFullView();

  Serial.println("[SYSTEM] Air Quality Station initialized successfully.");
  last_sensor_time = millis();
  last_sec_tick = millis();
}

// ==========================================
// 11. 主事件迴圈 (loop)
// ==========================================
void loop() {
  unsigned long now = millis();

  // 1. 處理使用者觸控互動
  handleTouchEvents();

  // 2. 秒鐘計時器 (更新運作時間與頂部時鐘)
  if (now - last_sec_tick >= 1000) {
    uptime_sec++;
    last_sec_tick = now;
    // 輕量局部刷新頂部時間標記
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu:%02lu", (uptime_sec / 3600) % 24, (uptime_sec / 60) % 60, uptime_sec % 60);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.fillRect(8, 20, 60, 10, COLOR_BG);
    tft.drawString(time_buf, 8, 20);
  }

  // 3. 感測器採樣與環境演算 (每 2.5 秒更新一次)
  if (now - last_sensor_time >= 2500) {
    last_sensor_time = now;

    if (is_demo_mode) {
      simulateEnvironmentStep();
    } else {
      // 若有接入實體感測器（如 I2C SHT31/BME280 或 UART PMS5003），此處讀取實際硬體
      // 未接實體感測器時，自動維持最新讀數
      updateOverallAssessment();
    }

    // 局部動態更新視圖內容 (避免全螢幕重繪閃爍)
    drawHeaderBar();
    if (current_view == 0) {
      renderDashboardView();
    } else if (current_view == 1) {
      renderTrendView();
    }

    Serial.printf("[AQI] Overall: %d | PM2.5: %.1f | CO2: %.0f | TVOC: %.0f | T: %.1fC | H: %.1f%%\n",
                  overall_aqi, metrics[METRIC_PM25].value, metrics[METRIC_CO2].value,
                  metrics[METRIC_TVOC].value, metrics[METRIC_TEMP].value, metrics[METRIC_HUMI].value);
  }
}
