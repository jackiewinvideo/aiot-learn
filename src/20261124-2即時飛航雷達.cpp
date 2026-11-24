#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ==========================================
// 1. 使用者設定區 (WiFi 與地理位置)
// ==========================================
const char* WIFI_SSID = "mcugust";              // 您的 WiFi 名稱
const char* WIFI_PASS = "mcu18226";             // 您的 WiFi 密碼

// 觀測中心點座標（預設以台灣桃園國際機場空域為例，可替換為住家經緯度）
float home_lat = 25.0797f;
float home_lon = 121.2342f;

// 預設雷達掃描半徑 (海浬 NM)：可透過觸控切換 10 -> 25 -> 50 -> 100 NM
float radar_range_nm = 25.0f;

// ==========================================
// 2. 硬體與驅動定義
// ==========================================
TFT_eSPI tft = TFT_eSPI();

// CYD 專屬 XPT2046 觸控 SPI 腳位
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ==========================================
// 3. 飛航資料結構
// ==========================================
struct Aircraft {
  char flight[10];     // 航班呼號 (如 EVA21, CAL008)
  char type[6];        // 機型代號 (如 B77W, A359)
  float lat;
  float lon;
  int altitude;        // 高度 (英尺 ft)
  int track;           // 航向 (0~359 度)
  int speed;           // 地速 (節 knots)
  float dist_nm;       // 與中心點距離 (海浬)
  int screen_x;        // 雷達螢幕 X
  int screen_y;        // 雷達螢幕 Y
};

#define MAX_AIRCRAFT 35
Aircraft aircrafts[MAX_AIRCRAFT];
int aircraft_count = 0;
int selected_idx = 0;
bool alert_active = false;
bool is_live_data = false;
int last_http_code = 0;
unsigned long last_network_update = 0;
bool need_radar_redraw = true;

SemaphoreHandle_t dataMutex;

// ==========================================
// 4. 內建空域模擬資料 (WiFi 未連線或無訊號時自動載入)
// ==========================================
void loadDemoAirspace() {
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000))) {
    struct DemoPlane {
      const char* f;
      const char* t;
      float d_lat;
      float d_lon;
      int alt;
      int trk;
      int spd;
    } demo_list[] = {
      {"EVA012", "B77W",  0.0318f,  0.0549f,  2025,  51, 208}, // 桃園進場
      {"CAL507", "B773", -0.0851f,  0.0670f, 36000, 228, 413}, // 巡航過境
      {"EVA163", "A321",  0.3093f,  0.2168f, 13525, 229, 325}, // 北部離場
      {"SJX762", "A21N", -0.2129f, -0.3001f,  3700,  69, 148}, // 新竹外海
      {"TTW721", "A320", -0.0157f, -0.0296f,   850,  49, 128}, // 短五邊
      {"APJ623", "A20N", -0.0961f, -0.1091f,  2100,  48, 143}, // 苗栗沿海
      {"EVA381", "A333",  0.1887f, -0.0774f, 10500, 297, 268}, // 西北出境
      {"SJX319", "A21N", -0.3777f, -0.0895f, 11475, 221, 350}  // 南下航線
    };

    aircraft_count = 8;
    for (int i = 0; i < 8; i++) {
      strlcpy(aircrafts[i].flight, demo_list[i].f, sizeof(aircrafts[i].flight));
      strlcpy(aircrafts[i].type, demo_list[i].t, sizeof(aircrafts[i].type));
      aircrafts[i].lat = home_lat + demo_list[i].d_lat;
      aircrafts[i].lon = home_lon + demo_list[i].d_lon;
      aircrafts[i].altitude = demo_list[i].alt;
      aircrafts[i].track = demo_list[i].trk;
      aircrafts[i].speed = demo_list[i].spd;

      float dy_nm = demo_list[i].d_lat * 60.0f;
      float dx_nm = demo_list[i].d_lon * 60.0f * cos(home_lat * 0.0174532925f);
      aircrafts[i].dist_nm = sqrt(dx_nm * dx_nm + dy_nm * dy_nm);
      aircrafts[i].screen_x = 120 + (int)((dx_nm / radar_range_nm) * 105.0f);
      aircrafts[i].screen_y = 120 - (int)((dy_nm / radar_range_nm) * 105.0f);
    }
    is_live_data = false;
    need_radar_redraw = true;
    xSemaphoreGive(dataMutex);
  }
}

// ==========================================
// 5. 高度顏色等級輔助函式
// ==========================================
uint16_t getAltitudeColor(int alt) {
  if (alt <= 0) return TFT_LIGHTGREY;
  if (alt < 3000) return TFT_MAGENTA;     // 超低空/起降
  if (alt < 10000) return TFT_CYAN;       // 低空進場
  if (alt < 24000) return TFT_GREEN;      // 爬升/巡航
  if (alt < 35000) return TFT_YELLOW;     // 巡航高度
  return TFT_ORANGE;                      // 超高空巡航
}

// ==========================================
// 6. 繪製航向小箭頭 (等腰三角圖標)
// ==========================================
void drawAirplaneIcon(int x, int y, int heading_deg, uint16_t color) {
  float rad = (heading_deg - 90.0f) * 0.0174532925f;
  int len = 7;
  int wing = 4;

  int x0 = x + (int)(cos(rad) * len);
  int y0 = y + (int)(sin(rad) * len);
  int x1 = x + (int)(cos(rad + 2.5f) * wing);
  int y1 = y + (int)(sin(rad + 2.5f) * wing);
  int x2 = x + (int)(cos(rad - 2.5f) * wing);
  int y2 = y + (int)(sin(rad - 2.5f) * wing);

  tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

// ==========================================
// 7. 繪製雷達靜態底圖 (同心圓與方位標線)
// ==========================================
void drawRadarBaseGrid() {
  const int cx = 120;
  const int cy = 120;
  const int r = 108;

  // 繪製雷達外框與同心圓
  tft.drawCircle(cx, cy, r, TFT_DARKGREEN);
  tft.drawCircle(cx, cy, (int)(r * 0.66f), 0x0200);
  tft.drawCircle(cx, cy, (int)(r * 0.33f), 0x0200);

  // 繪製十字瞄準線 (N-S, E-W)
  tft.drawFastHLine(cx - r, cy, r * 2, 0x0200);
  tft.drawFastVLine(cx, cy - r, r * 2, 0x0200);

  // 中心點標記 (HOME)
  tft.fillCircle(cx, cy, 2, TFT_RED);

  // 方位標示文字 (N, S, W, E)
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("N", cx - 3, cy - r + 3, 1);
  tft.drawString("S", cx - 3, cy + r - 10, 1);
  tft.drawString("W", cx - r + 4, cy - 4, 1);
  tft.drawString("E", cx + r - 10, cy - 4, 1);
}

// ==========================================
// 8. Core 0: 背景網路下載任務 (FreeRTOS)
// ==========================================
void TaskNetwork(void *pvParameters) {
  WiFiClientSecure client;
  client.setInsecure(); // 略過憑證驗證
  client.setTimeout(8);

  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.setTimeout(7000);

      // 使用 adsb.lol 免費開放 API
      String url = "https://api.adsb.lol/v2/point/" +
                   String(home_lat, 4) + "/" +
                   String(home_lon, 4) + "/" +
                   String((int)radar_range_nm);

      Serial.println("\n[RADAR] Requesting: " + url);

      if (http.begin(client, url)) {
        http.setUserAgent("Mozilla/5.0 (ESP32-CYD-Radar)");
        http.addHeader("Accept", "application/json");
        int httpCode = http.GET();
        last_http_code = httpCode;

        Serial.printf("[RADAR] HTTP Response Code: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          Serial.printf("[RADAR] Received: %d bytes\n", payload.length());

          // ArduinoJson 7 串流過濾器
          JsonDocument filter;
          filter["ac"][0]["flight"]   = true;
          filter["ac"][0]["t"]        = true;
          filter["ac"][0]["lat"]      = true;
          filter["ac"][0]["lon"]      = true;
          filter["ac"][0]["alt_baro"] = true;
          filter["ac"][0]["track"]    = true;
          filter["ac"][0]["gs"]       = true;

          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

          if (!err) {
            JsonArray arr = doc["ac"].as<JsonArray>();
            int count = 0;
            bool found_proximity_alert = false;

            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000))) {
              for (JsonObject item : arr) {
                if (count >= MAX_AIRCRAFT) break;
                if (!item["lat"].is<float>() || !item["lon"].is<float>()) continue;

                float flat = item["lat"];
                float flon = item["lon"];

                // 等距方位投影計算距離 (海浬)
                float dy_nm = (flat - home_lat) * 60.0f;
                float dx_nm = (flon - home_lon) * 60.0f * cos(home_lat * 0.0174532925f);
                float dist = sqrt(dx_nm * dx_nm + dy_nm * dy_nm);

                if (dist <= radar_range_nm) {
                  const char* f_str = item["flight"] | "---";
                  while (*f_str == ' ') f_str++;
                  strlcpy(aircrafts[count].flight, f_str, sizeof(aircrafts[count].flight));
                  int flen = strlen(aircrafts[count].flight);
                  while (flen > 0 && aircrafts[count].flight[flen - 1] == ' ') {
                    aircrafts[count].flight[flen - 1] = '\0';
                    flen--;
                  }

                  strlcpy(aircrafts[count].type, item["t"] | "UNKN", sizeof(aircrafts[count].type));
                  aircrafts[count].lat = flat;
                  aircrafts[count].lon = flon;

                  if (item["alt_baro"].is<int>()) {
                    aircrafts[count].altitude = item["alt_baro"].as<int>();
                  } else {
                    aircrafts[count].altitude = 0;
                  }

                  aircrafts[count].track = item["track"] | 0;
                  aircrafts[count].speed = item["gs"] | 0;
                  aircrafts[count].dist_nm = dist;

                  // 螢幕座標映射 (中心 cx=120, cy=120, 半徑 105px)
                  aircrafts[count].screen_x = 120 + (int)((dx_nm / radar_range_nm) * 105.0f);
                  aircrafts[count].screen_y = 120 - (int)((dy_nm / radar_range_nm) * 105.0f);

                  if (dist < 5.0f && aircrafts[count].altitude > 0 && aircrafts[count].altitude < 3500) {
                    found_proximity_alert = true;
                  }

                  count++;
                }
              }

              if (count > 0) {
                aircraft_count = count;
                is_live_data = true;
              }
              if (selected_idx >= aircraft_count) selected_idx = 0;
              alert_active = found_proximity_alert;
              last_network_update = millis();
              need_radar_redraw = true;
              xSemaphoreGive(dataMutex);

              Serial.printf("[RADAR] Live Aircrafts in range: %d\n", count);

              // 板載 RGB LED 警示指示 (Active LOW)
              if (alert_active) {
                digitalWrite(CYD_LED_RED, LOW);   // 亮紅燈警示
                digitalWrite(CYD_LED_BLUE, HIGH);
              } else {
                digitalWrite(CYD_LED_RED, HIGH);
                digitalWrite(CYD_LED_BLUE, LOW);  // 亮藍燈表示正常連線
              }
            }
          } else {
            Serial.printf("[RADAR] JSON Error: %s\n", err.c_str());
          }
        }
        http.end();
      }
    } else {
      // WiFi 未連線時，保持 Demo 模式
      last_http_code = -2;
    }
    vTaskDelay(pdMS_TO_TICKS(6000));
  }
}

// ==========================================
// 9. 右側資訊面板渲染 (240~320, 0~240)
// ==========================================
void updateInfoPanel() {
  tft.fillRect(241, 0, 79, 240, 0x0841); // 深灰藍底色
  tft.drawFastVLine(240, 0, 240, TFT_GREEN);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_GREEN, 0x0841);
  tft.drawString("AIR RADAR", 245, 6, 2);

  // 航班統計與狀態
  tft.setTextColor(TFT_WHITE, 0x0841);
  tft.drawString("Air: " + String(aircraft_count), 245, 26, 2);

  if (is_live_data) {
    if (alert_active) {
      tft.setTextColor(TFT_RED, 0x0841);
      tft.drawString("! PROX !", 245, 46, 2);
    } else {
      tft.setTextColor(TFT_GREEN, 0x0841);
      tft.drawString("LIVE: OK", 245, 46, 1);
    }
  } else {
    tft.setTextColor(TFT_YELLOW, 0x0841);
    tft.drawString("DEMO AIR", 245, 46, 1);
  }

  tft.drawFastHLine(245, 62, 70, TFT_DARKGREY);

  // 當前選取飛機詳細資訊
  if (aircraft_count > 0 && selected_idx < aircraft_count) {
    tft.setTextColor(TFT_YELLOW, 0x0841);
    tft.drawString(aircrafts[selected_idx].flight, 245, 68, 2);

    tft.setTextColor(TFT_CYAN, 0x0841);
    tft.drawString("T: " + String(aircrafts[selected_idx].type), 245, 88, 2);

    tft.setTextColor(TFT_WHITE, 0x0841);
    tft.drawString("Alt:", 245, 108, 1);
    tft.setTextColor(getAltitudeColor(aircrafts[selected_idx].altitude), 0x0841);
    tft.drawString(String(aircrafts[selected_idx].altitude) + "ft", 245, 118, 2);

    tft.setTextColor(TFT_WHITE, 0x0841);
    tft.drawString("Spd: " + String(aircrafts[selected_idx].speed) + "kt", 245, 138, 2);
    tft.drawString("Dis: " + String(aircrafts[selected_idx].dist_nm, 1) + "nm", 245, 156, 2);
    tft.drawString("Hdg: " + String(aircrafts[selected_idx].track) + "°", 245, 174, 2);
  } else {
    tft.setTextColor(TFT_LIGHTGREY, 0x0841);
    tft.drawString("Scanning", 245, 90, 2);
    tft.drawString("Airspace", 245, 110, 2);
  }

  tft.drawFastHLine(245, 196, 70, TFT_DARKGREY);

  // 範圍按鈕區塊 (底部可點擊)
  tft.fillRect(245, 204, 70, 28, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String((int)radar_range_nm) + " NM", 280, 218, 2);
  tft.setTextDatum(TL_DATUM);
}

// ==========================================
// 10. 系統設定 (setup)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- ESP32 CYD ADS-B Radar Initializing ---");

  // 初始化板載 RGB LED (Active LOW)
  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);
  digitalWrite(CYD_LED_RED, HIGH);
  digitalWrite(CYD_LED_GREEN, HIGH);
  digitalWrite(CYD_LED_BLUE, HIGH);

  dataMutex = xSemaphoreCreateMutex();

  // 初始化 TFT 螢幕 (橫向模式 320x240)
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // 初始化 XPT2046 觸控螢幕
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 先載入台灣桃園空域模擬展示航班，確保螢幕一開機即有飛機！
  loadDemoAirspace();

  // 顯示連線提示畫面
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("ADS-B FLIGHT RADAR", 45, 80, 4);
  tft.drawString("Connecting: " + String(WIFI_SSID), 45, 120, 2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(400);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    tft.drawString("WiFi OK! Fetching ADS-B...", 45, 150, 2);
    digitalWrite(CYD_LED_GREEN, LOW);
    delay(600);
    digitalWrite(CYD_LED_GREEN, HIGH);
  } else {
    Serial.println("\nWiFi timeout! Starting in Airspace Demo Mode.");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Starting in Demo Mode...", 45, 150, 2);
    delay(1000);
  }

  tft.fillScreen(TFT_BLACK);
  drawRadarBaseGrid();
  updateInfoPanel();

  // 啟動 Core 0 背景網路任務
  xTaskCreatePinnedToCore(TaskNetwork, "TaskNetwork", 10240, NULL, 1, NULL, 0);
}

// ==========================================
// 11. Core 1: 主迴圈 (雷達掃描與互動)
// ==========================================
float sweep_angle = 0.0f;
float last_sweep_angle = 0.0f;
unsigned long last_panel_refresh = 0;

void loop() {
  const int cx = 120;
  const int cy = 120;
  const int r = 106;

  // 1. 每當有新網路資料或半徑切換時，清空雷達重繪
  if (need_radar_redraw) {
    tft.fillCircle(cx, cy, 107, TFT_BLACK);
    drawRadarBaseGrid();
    need_radar_redraw = false;
  }

  // 2. 旋轉雷達掃描線動畫 (30~45 FPS)
  float rad = sweep_angle * 0.0174532925f;
  int sx = cx + (int)(cos(rad) * r);
  int sy = cy + (int)(sin(rad) * r);

  float last_rad = last_sweep_angle * 0.0174532925f;
  int old_sx = cx + (int)(cos(last_rad) * r);
  int old_sy = cy + (int)(sin(last_rad) * r);

  // 清除上一幀掃描線
  tft.drawLine(cx, cy, old_sx, old_sy, TFT_BLACK);
  // 繪製新綠色掃描線
  tft.drawLine(cx, cy, sx, sy, 0x07E0);

  // 局部修補雷達同心圓底圖
  drawRadarBaseGrid();

  last_sweep_angle = sweep_angle;
  sweep_angle += 2.5f;
  if (sweep_angle >= 360.0f) sweep_angle = 0.0f;

  // 3. 繪製所有空域飛機（三角圖示 + 航班呼號標籤 + 選取框）
  if (xSemaphoreTake(dataMutex, 0)) {
    for (int i = 0; i < aircraft_count; i++) {
      uint16_t color = getAltitudeColor(aircrafts[i].altitude);
      int ax = aircrafts[i].screen_x;
      int ay = aircrafts[i].screen_y;

      if (ax >= 15 && ax <= 225 && ay >= 15 && ay <= 225) {
        // 繪製飛機三角形
        drawAirplaneIcon(ax, ay, aircrafts[i].track, color);

        // 在飛機右上方標示呼號 (例如 EVA012)
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(aircrafts[i].flight, ax + 6, ay - 6, 1);

        // 被選取的飛機加畫外圈
        if (i == selected_idx) {
          tft.drawCircle(ax, ay, 9, TFT_WHITE);
        }
      }
    }
    xSemaphoreGive(dataMutex);
  }

  // 4. 定時更新右側資訊欄
  if (millis() - last_panel_refresh > 1000) {
    updateInfoPanel();
    last_panel_refresh = millis();
  }

  // 5. 觸控偵測
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int tx = map(p.x, 240, 3800, 0, 320);
    int ty = map(p.y, 240, 3800, 0, 240);

    // 點擊右下角切換半徑 (10 -> 25 -> 50 -> 100 NM)
    if (tx >= 235 && ty >= 190) {
      if (radar_range_nm == 10.0f) radar_range_nm = 25.0f;
      else if (radar_range_nm == 25.0f) radar_range_nm = 50.0f;
      else if (radar_range_nm == 50.0f) radar_range_nm = 100.0f;
      else radar_range_nm = 10.0f;

      // 重新計算飛機螢幕座標
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(500))) {
        for (int i = 0; i < aircraft_count; i++) {
          float dy_nm = (aircrafts[i].lat - home_lat) * 60.0f;
          float dx_nm = (aircrafts[i].lon - home_lon) * 60.0f * cos(home_lat * 0.0174532925f);
          aircrafts[i].screen_x = 120 + (int)((dx_nm / radar_range_nm) * 105.0f);
          aircrafts[i].screen_y = 120 - (int)((dy_nm / radar_range_nm) * 105.0f);
        }
        need_radar_redraw = true;
        xSemaphoreGive(dataMutex);
      }
      updateInfoPanel();
      delay(250);
    } 
    // 點擊左側雷達切換或選取最近的飛機
    else if (tx < 235) {
      if (xSemaphoreTake(dataMutex, 0)) {
        int best_i = -1;
        float min_dist_px = 35.0f;
        for (int i = 0; i < aircraft_count; i++) {
          float d = hypot(tx - aircrafts[i].screen_x, ty - aircrafts[i].screen_y);
          if (d < min_dist_px) {
            min_dist_px = d;
            best_i = i;
          }
        }
        if (best_i != -1) {
          selected_idx = best_i;
        } else if (aircraft_count > 0) {
          selected_idx = (selected_idx + 1) % aircraft_count;
        }
        xSemaphoreGive(dataMutex);
        updateInfoPanel();
        delay(200);
      }
    }
  }

  delay(20);
}
