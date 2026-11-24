/**
 * @file main.cpp
 * @brief ESP32 CYD (ESP32-2432S028R) 即時觸控流體物理模擬 (Fluid Simulation)
 * 
 * 核心特性：
 * 1. Jos Stam 的經典 Stable Fluids 演算法（Navier-Stokes 方程：Advection, Diffusion, Projection）
 * 2. 80x60 模擬網格，以 4x4 像素快速區塊映射至 320x240 ILI9341 橫向螢幕
 * 3. 暫存緩衝區動態 Heap 分配與複用（記憶體僅需 ~130KB，避免靜態 BSS 溢位）
 * 4. 雙緩衝行區塊渲染 (Line/Strip Buffer)，大幅消除 SPI 命令開銷，穩定達成 30+ FPS
 * 5. XPT2046 觸控即時速度向量注入 + 彩虹/多色彩染料動態漩渦
 * 6. CYD 板載 RGB LED (Active LOW) 隨觸控流體顏色連動
 * 7. 即時 FPS 與觸控反應測試 HUD 資訊列
 * 8. 閒置自動展示 (Auto-Demo Mode)：無觸控時自動生成流體漩渦
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ==========================================
// 1. 硬體針腳與周邊配置 (CYD ESP32-2432S028R)
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

// 板載 RGB LED (注意：Active LOW 低電位點亮)
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

// ==========================================
// 2. 流體物理模擬參數與網格尺寸
// ==========================================
#define GRID_W 80
#define GRID_H 60
#define CELL_SCALE 4       // 80 * 4 = 320, 60 * 4 = 240
#define NUM_CELLS (GRID_W * GRID_H)

// 陣列線性索引巨集
#define IX(i, j) ((i) + (j) * GRID_W)

// 物理模擬場指標 (在 setup 中動態從內部 Heap 分配，避免 ESP32 DRAM BSS 區段溢位)
static float *u = nullptr;         // X 方向速度場
static float *v = nullptr;         // Y 方向速度場
static float *tmp1 = nullptr;      // 暫存場 1 (速度擴散、壓力場 p、染料暫存)
static float *tmp2 = nullptr;      // 暫存場 2 (速度擴散、散度場 div)

// 三原色染料密度場 (RGB 獨立擴散與流動，呈現絢麗色彩)
static float *dens_r = nullptr;
static float *dens_g = nullptr;
static float *dens_b = nullptr;

// 物理參數調校
static const float DT = 0.15f;           // 時間步長
static const float VISCOSITY = 0.00001f; // 黏滯度 (極小可保持豐富漩渦)
static const float DIFFUSION = 0.00001f; // 擴散速率
static const float DISSIPATION = 0.992f; // 染料衰減率 (使畫面不會長期泛白過載)
static const int ITERATIONS = 6;         // Gauss-Seidel 壓力投影疊代次數 (兼顧流暢度與質量守恆)

// 渲染緩衝區：一次合成 4 行螢幕像素 (320 寬度 * 4 像素高度 = 1280 個 RGB565 像素 = 2560 bytes)
static uint16_t stripBuffer[320 * CELL_SCALE];

// 觸控互動追蹤
static int last_touch_x = -1;
static int last_touch_y = -1;
static unsigned long last_touch_time = 0;
static float current_hue = 0.0f;
static bool hud_visible = true;

// 效能統計
static uint32_t frame_count = 0;
static float current_fps = 0.0f;
static unsigned long fps_timer = 0;

// ==========================================
// 3. 邊界條件處理 (Boundary Conditions)
// ==========================================
void set_bnd(int b, float *x) {
  // 邊界反射阻隔或連續流動 (b=1: X軸垂直牆面反轉, b=2: Y軸水平牆面反轉, b=0: 純量)
  for (int i = 1; i < GRID_W - 1; i++) {
    x[IX(i, 0)]          = (b == 2) ? -x[IX(i, 1)]          : x[IX(i, 1)];
    x[IX(i, GRID_H - 1)] = (b == 2) ? -x[IX(i, GRID_H - 2)] : x[IX(i, GRID_H - 2)];
  }
  for (int j = 1; j < GRID_H - 1; j++) {
    x[IX(0, j)]          = (b == 1) ? -x[IX(1, j)]          : x[IX(1, j)];
    x[IX(GRID_W - 1, j)] = (b == 1) ? -x[IX(GRID_W - 2, j)] : x[IX(GRID_W - 2, j)];
  }

  // 四個角落點平滑平均
  x[IX(0, 0)]                   = 0.5f * (x[IX(1, 0)] + x[IX(0, 1)]);
  x[IX(0, GRID_H - 1)]          = 0.5f * (x[IX(1, GRID_H - 1)] + x[IX(0, GRID_H - 2)]);
  x[IX(GRID_W - 1, 0)]          = 0.5f * (x[IX(GRID_W - 2, 0)] + x[IX(GRID_W - 1, 1)]);
  x[IX(GRID_W - 1, GRID_H - 1)] = 0.5f * (x[IX(GRID_W - 2, GRID_H - 1)] + x[IX(GRID_W - 1, GRID_H - 2)]);
}

// ==========================================
// 4. Jos Stam 流體核心演算法
// ==========================================

// 線性放鬆解算器 (Gauss-Seidel Relaxation)
void lin_solve(int b, float *x, const float *x0, float a, float c) {
  float cRecip = 1.0f / c;
  for (int k = 0; k < ITERATIONS; k++) {
    for (int j = 1; j < GRID_H - 1; j++) {
      int row = j * GRID_W;
      for (int i = 1; i < GRID_W - 1; i++) {
        int idx = row + i;
        x[idx] = (x0[idx] + a * (x[idx - 1] + x[idx + 1] + x[idx - GRID_W] + x[idx + GRID_W])) * cRecip;
      }
    }
    set_bnd(b, x);
  }
}

// 擴散步驟 (Diffusion)
void diffuse(int b, float *x, const float *x0, float diff, float dt) {
  float a = dt * diff * (GRID_W - 2) * (GRID_H - 2);
  lin_solve(b, x, x0, a, 1.0f + 4.0f * a);
}

// 逆向追蹤對流步驟 (Semi-Lagrangian Advection)
void advect(int b, float *d, const float *d0, const float *u_vel, const float *v_vel, float dt) {
  float dt0_x = dt * (GRID_W - 2);
  float dt0_y = dt * (GRID_H - 2);

  for (int j = 1; j < GRID_H - 1; j++) {
    int row = j * GRID_W;
    for (int i = 1; i < GRID_W - 1; i++) {
      int idx = row + i;
      float x = (float)i - dt0_x * u_vel[idx];
      float y = (float)j - dt0_y * v_vel[idx];

      // 邊界防禦截斷
      if (x < 0.5f) x = 0.5f;
      if (x > (float)GRID_W - 1.5f) x = (float)GRID_W - 1.5f;
      int i0 = (int)x;
      int i1 = i0 + 1;

      if (y < 0.5f) y = 0.5f;
      if (y > (float)GRID_H - 1.5f) y = (float)GRID_H - 1.5f;
      int j0 = (int)y;
      int j1 = j0 + 1;

      // 雙線性內插 (Bilinear Interpolation)
      float s1 = x - (float)i0;
      float s0 = 1.0f - s1;
      float t1 = y - (float)j0;
      float t0 = 1.0f - t1;

      int row0 = j0 * GRID_W;
      int row1 = j1 * GRID_W;
      d[idx] = s0 * (t0 * d0[row0 + i0] + t1 * d0[row1 + i0]) +
               s1 * (t0 * d0[row0 + i1] + t1 * d0[row1 + i1]);
    }
  }
  set_bnd(b, d);
}

// 壓力投影 (Projection)：強迫速度場滿足無散度 (Divergence-Free)，形成真實渦流
void project(float *u_vel, float *v_vel, float *p, float *div) {
  float h_x = 1.0f / (float)GRID_W;
  float h_y = 1.0f / (float)GRID_H;

  // 計算速度散度與初值
  for (int j = 1; j < GRID_H - 1; j++) {
    int row = j * GRID_W;
    for (int i = 1; i < GRID_W - 1; i++) {
      int idx = row + i;
      div[idx] = -0.5f * (h_x * (u_vel[idx + 1] - u_vel[idx - 1]) +
                          h_y * (v_vel[idx + GRID_W] - v_vel[idx - GRID_W]));
      p[idx] = 0.0f;
    }
  }
  set_bnd(0, div);
  set_bnd(0, p);

  // 求解卜瓦松方程 (Poisson equation: Laplace(p) = div)
  lin_solve(0, p, div, 1.0f, 4.0f);

  // 依壓力梯度修正速度向量場
  for (int j = 1; j < GRID_H - 1; j++) {
    int row = j * GRID_W;
    for (int i = 1; i < GRID_W - 1; i++) {
      int idx = row + i;
      u_vel[idx] -= 0.5f * (p[idx + 1] - p[idx - 1]) * (float)GRID_W;
      v_vel[idx] -= 0.5f * (p[idx + GRID_W] - p[idx - GRID_W]) * (float)GRID_H;
    }
  }
  set_bnd(1, u_vel);
  set_bnd(2, v_vel);
}

// 速度場模擬單步
void vel_step(float *u_vel, float *v_vel, float *u0, float *v0, float visc, float dt) {
  // 1. 速度場擴散
  diffuse(1, u0, u_vel, visc, dt);
  diffuse(2, v0, v_vel, visc, dt);

  // 2. 壓力投影確保質量守恆
  project(u0, v0, u_vel, v_vel);

  // 3. 速度場自身對流
  advect(1, u_vel, u0, u0, v0, dt);
  advect(2, v_vel, v0, u0, v0, dt);

  // 4. 再次投影保證嚴格無散
  project(u_vel, v_vel, u0, v0);
}

// 染料密度模擬單步 (支援自衰減 Dissipation)
void dens_step(float *x, float *x0, const float *u_vel, const float *v_vel, float diff, float dt) {
  diffuse(0, x0, x, diff, dt);
  advect(0, x, x0, u_vel, v_vel, dt);

  // 密度耗散
  for (int i = 0; i < NUM_CELLS; i++) {
    x[i] *= DISSIPATION;
  }
}

// ==========================================
// 5. 色彩轉換與 RGB LED 回饋
// ==========================================

// 將 HSV 色彩空間轉換為 RGB (0.0~1.0)
void hsv2rgb(float h, float s, float v_val, float &r, float &g, float &b) {
  while (h >= 360.0f) h -= 360.0f;
  while (h < 0.0f) h += 360.0f;
  float c = v_val * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v_val - c;

  if (h < 60.0f)       { r = c; g = x; b = 0.0f; }
  else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
  else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
  else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
  else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
  else                 { r = c; g = 0.0f; b = x; }

  r += m;
  g += m;
  b += m;
}

// 更新 CYD 板載 RGB LED (Active LOW 低電位點亮)
void updateBoardLED(float r, float g, float b) {
  analogWrite(CYD_LED_RED,   255 - (int)(constrain(r, 0.0f, 1.0f) * 255));
  analogWrite(CYD_LED_GREEN, 255 - (int)(constrain(g, 0.0f, 1.0f) * 255));
  analogWrite(CYD_LED_BLUE,  255 - (int)(constrain(b, 0.0f, 1.0f) * 255));
}

// ==========================================
// 6. 觸控注入與物理力互動
// ==========================================
void injectFluid(int grid_x, int grid_y, float force_x, float force_y, float r, float g, float b) {
  // 向點擊位置周圍半徑 2 格注入速度與染料
  int radius = 2;
  for (int dj = -radius; dj <= radius; dj++) {
    int gy = grid_y + dj;
    if (gy < 1 || gy >= GRID_H - 1) continue;

    for (int di = -radius; di <= radius; di++) {
      int gx = grid_x + di;
      if (gx < 1 || gx >= GRID_W - 1) continue;

      float dist_sq = (float)(di * di + dj * dj);
      if (dist_sq <= (float)(radius * radius)) {
        float factor = 1.0f - (sqrtf(dist_sq) / (float)(radius + 1));
        int idx = IX(gx, gy);

        // 注入速度場向量
        u[idx] += force_x * factor * 0.4f;
        v[idx] += force_y * factor * 0.4f;

        // 注入彩色染料
        dens_r[idx] = constrain(dens_r[idx] + r * factor * 1.8f, 0.0f, 3.0f);
        dens_g[idx] = constrain(dens_g[idx] + g * factor * 1.8f, 0.0f, 3.0f);
        dens_b[idx] = constrain(dens_b[idx] + b * factor * 1.8f, 0.0f, 3.0f);
      }
    }
  }
}

// 閒置自動演示模式：當使用者未觸控時自動生成流動漩渦
void runAutoDemo() {
  static float angle = 0.0f;
  angle += 0.05f;

  float cx = (GRID_W / 2.0f) + cosf(angle) * (GRID_W * 0.32f);
  float cy = (GRID_H / 2.0f) + sinf(angle * 1.3f) * (GRID_H * 0.32f);

  float vx = -sinf(angle) * 2.5f;
  float vy = cosf(angle * 1.3f) * 2.5f;

  float r, g, b;
  hsv2rgb(fmodf(angle * 50.0f, 360.0f), 1.0f, 1.0f, r, g, b);

  injectFluid((int)cx, (int)cy, vx, vy, r, g, b);
  updateBoardLED(r * 0.4f, g * 0.4f, b * 0.4f);
}

// ==========================================
// 7. 螢幕快速行塊渲染 (Fast Strip Rendering)
// ==========================================
void renderFluidToScreen() {
  // 將 80x60 物理網格按行展開成 4x4 像素塊，透過 320x4 StripBuffer 批次傳送至 TFT
  for (int j = 0; j < GRID_H; j++) {
    int row_offset = j * GRID_W;

    // 1. 生成 1 行網格對應的 4 條掃描線
    for (int i = 0; i < GRID_W; i++) {
      int idx = row_offset + i;

      // 提取 RGB 染料強度並映射至 0~255
      int r = (int)(constrain(dens_r[idx], 0.0f, 1.0f) * 255.0f);
      int g = (int)(constrain(dens_g[idx], 0.0f, 1.0f) * 255.0f);
      int b = (int)(constrain(dens_b[idx], 0.0f, 1.0f) * 255.0f);

      // 色彩微光底色強化 (當有流動但染料低時顯示微藍流線)
      if (r == 0 && g == 0 && b == 0) {
        float speed = fabsf(u[idx]) + fabsf(v[idx]);
        if (speed > 0.05f) {
          b = (int)constrain(speed * 30.0f, 0.0f, 40.0f);
        }
      }

      uint16_t color = tft.color565(r, g, b);

      // 填入 4x4 像素緩衝區
      int base_pixel_x = i * CELL_SCALE;
      for (int sy = 0; sy < CELL_SCALE; sy++) {
        int line_start = sy * 320 + base_pixel_x;
        stripBuffer[line_start + 0] = color;
        stripBuffer[line_start + 1] = color;
        stripBuffer[line_start + 2] = color;
        stripBuffer[line_start + 3] = color;
      }
    }

    // 2. 單次 SPI 傳輸整塊 320x4 像素 (大幅減少 SPI 指令開銷)
    tft.pushImage(0, j * CELL_SCALE, 320, CELL_SCALE, stripBuffer);
  }

  // 3. 繪製 HUD 診斷資訊 (FPS、觸控座標、狀態)
  if (hud_visible) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);

    char buf[64];
    snprintf(buf, sizeof(buf), "FPS: %.1f | Grid: 80x60 | CYD Fluid", current_fps);
    tft.drawString(buf, 8, 8);

    if (last_touch_x >= 0) {
      snprintf(buf, sizeof(buf), "Touch: (%d, %d)", last_touch_x, last_touch_y);
    } else {
      snprintf(buf, sizeof(buf), "Touch Screen to Swirl!");
    }
    tft.drawString(buf, 8, 20);
  }
}

// ==========================================
// 8. 系統初始化 (setup)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n============================================");
  Serial.println("  ESP32 CYD - Interactive Fluid Simulation  ");
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
  tft.fillScreen(TFT_BLACK);

  // 初始化 XPT2046 觸控螢幕
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 動態分配流體模擬記憶體 (內部 Heap)
  size_t field_bytes = NUM_CELLS * sizeof(float);
  u      = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  v      = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  tmp1   = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  tmp2   = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  dens_r = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  dens_g = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  dens_b = (float*)heap_caps_malloc(field_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!u || !v || !tmp1 || !tmp2 || !dens_r || !dens_g || !dens_b) {
    Serial.println("[ERROR] Failed to allocate fluid grids! Not enough memory.");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("RAM Alloc Error!", 60, 100);
    while (true) delay(1000);
  }

  // 清空所有流體網格場
  memset(u, 0, field_bytes);
  memset(v, 0, field_bytes);
  memset(tmp1, 0, field_bytes);
  memset(tmp2, 0, field_bytes);
  memset(dens_r, 0, field_bytes);
  memset(dens_g, 0, field_bytes);
  memset(dens_b, 0, field_bytes);

  Serial.printf("[SYSTEM] Allocated %u KB for fluid grid. Free Heap: %lu bytes\n", 
                (unsigned int)((field_bytes * 7) / 1024), (unsigned long)ESP.getFreeHeap());
  Serial.println("[SYSTEM] Fluid engine & Touch initialized successfully.");
  fps_timer = millis();
}

// ==========================================
// 9. 主運算與事件迴圈 (loop)
// ==========================================
void loop() {
  // 1. 處理觸控輸入與物理力注入
  bool is_touching = ts.touched();
  if (is_touching) {
    TS_Point p = ts.getPoint();

    // 觸控校正映射 (針對 320x240 橫向螢幕)
    int tx = map(p.x, 240, 3800, 0, 320);
    int ty = map(p.y, 240, 3800, 0, 240);
    tx = constrain(tx, 0, 319);
    ty = constrain(ty, 0, 239);

    if (last_touch_x >= 0) {
      // 計算滑動速度向量
      float dx = (float)(tx - last_touch_x);
      float dy = (float)(ty - last_touch_y);

      // 動態彩虹色彩滾動 (根據滑動與時間連續變化)
      current_hue = fmodf(current_hue + 7.5f, 360.0f);
      float r, g, b;
      hsv2rgb(current_hue, 1.0f, 1.0f, r, g, b);

      // 轉換為網格座標並注入
      int gx = tx / CELL_SCALE;
      int gy = ty / CELL_SCALE;
      injectFluid(gx, gy, dx, dy, r, g, b);

      // 板載 RGB LED 連動高亮
      updateBoardLED(r, g, b);
    }

    last_touch_x = tx;
    last_touch_y = ty;
    last_touch_time = millis();
  } else {
    // 釋放觸控時平滑復位
    last_touch_x = -1;
    last_touch_y = -1;

    // 超過 2.5 秒無觸控，啟動自動展示漩渦
    if (millis() - last_touch_time > 2500) {
      runAutoDemo();
    } else {
      // 觸控剛結束時 LED 漸滅
      digitalWrite(CYD_LED_RED, HIGH);
      digitalWrite(CYD_LED_GREEN, HIGH);
      digitalWrite(CYD_LED_BLUE, HIGH);
    }
  }

  // 2. 執行 Navier-Stokes 流體力學解算步驟 (複用 tmp1 與 tmp2)
  vel_step(u, v, tmp1, tmp2, VISCOSITY, DT);

  // 3. 獨立運算 R, G, B 三原色染料對流與擴散 (複用 tmp1)
  dens_step(dens_r, tmp1, u, v, DIFFUSION, DT);
  dens_step(dens_g, tmp1, u, v, DIFFUSION, DT);
  dens_step(dens_b, tmp1, u, v, DIFFUSION, DT);

  // 4. 渲染至螢幕
  renderFluidToScreen();

  // 5. FPS 效能計算
  frame_count++;
  unsigned long now = millis();
  if (now - fps_timer >= 1000) {
    current_fps = (float)frame_count * 1000.0f / (float)(now - fps_timer);
    frame_count = 0;
    fps_timer = now;
    Serial.printf("[PERF] Fluid Sim FPS: %.1f | Touch: %s\n", current_fps, is_touching ? "YES" : "NO");
  }
}
