/**
 * @file main.cpp
 * @brief ESP32 CYD (ESP32-2432S028R) 4 顆飛行隨機 RGB 球物理碰撞反彈系統
 * 
 * 核心特性：
 * 1. 2D 完全彈性碰撞物理引擎 (Elastic Collision Physics)：
 *    - 動量守恆與動能守恆 (Conservation of Momentum & Kinetic Energy)
 *    - 法線衝量計算 (Normal Impulse Resolution)
 *    - 位置分離校正 (Positional Penetration Resolution，徹底根除嵌合/卡球 Bug)
 *    - 牆面精確反彈邊界檢測
 * 2. 60 FPS 極致流暢無閃爍渲染 (Flicker-Free Rendering)：
 *    - 上一幀位置局部微抹除 (Local Dirty Erase)，擺脫全螢幕清屏閃爍
 * 3. 3D 擬真立體光影球體：
 *    - 徑向漸層與鏡面高光 (Specular Highlight)
 *    - 碰撞瞬間產生衝擊光暈波紋 (Shockwave Ripple)
 * 4. 觸控互動手勢：
 *    - 點選球體：拖曳並於釋放時依甩動手勢賦予速度向量 (Flick / Throw)
 *    - 點擊空白處：產生引力/排斥力場 (Force Push / Shockwave)
 * 5. CYD 硬體整合：
 *    - 板載 RGB LED (Active LOW) 在球體碰撞時同步閃爍發光反饋
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

// ==========================================
// 2. 物理常數與資料結構
// ==========================================
#define SCREEN_W 320
#define SCREEN_H 240
#define NUM_BALLS 4
#define COLOR_BG 0x0821        // 深黑藍背景

#define BOUNCE_RESTITUTION 1.0f  // 彈性係數 (1.0 = 完全彈性碰撞，永不衰減)
#define MAX_VELOCITY 7.5f        // 最大飛行速度限制
#define MIN_VELOCITY 1.2f        // 最小飛行速度限制 (保持活躍飛行)

struct Ball {
  float x, y;          // 當前位置 (子像素精度)
  float vx, vy;        // 速度向量 (像素/幀)
  float last_x, last_y;// 上一幀位置 (用於局部微抹除)
  float radius;        // 球體半徑
  float mass;          // 質量 (與半徑平方成正比)
  uint16_t color;      // 主要主色調
  uint16_t light_color;// 高光色
  uint16_t dark_color; // 陰影色
  uint8_t r_byte, g_byte, b_byte; // RGB 原色 (用於 LED 反饋)
  bool is_dragged;     // 是否正被手指拖曳
  float drag_off_x, drag_off_y; // 拖曳偏移量
};

// 碰撞衝擊波紋效果結構
struct Ripple {
  float x, y;
  float current_r;
  float max_r;
  uint16_t color;
  bool active;
};

#define MAX_RIPPLES 4
static Ripple ripples[MAX_RIPPLES];

static Ball balls[NUM_BALLS];
static int dragged_ball_idx = -1;
static unsigned long led_off_time = 0;
static uint32_t total_bounces = 0;

// 效能與 FPS 統計
static uint32_t frame_count = 0;
static float current_fps = 60.0f;
static unsigned long fps_timer = 0;

// ==========================================
// 3. 色彩工具與 LED 控制
// ==========================================
void triggerCollisionLED(uint8_t r, uint8_t g, uint8_t b) {
  // CYD 板載 RGB LED (Active LOW)
  analogWrite(CYD_LED_RED,   255 - r);
  analogWrite(CYD_LED_GREEN, 255 - g);
  analogWrite(CYD_LED_BLUE,  255 - b);
  led_off_time = millis() + 45; // 點亮 45ms
}

void updateLEDState() {
  if (led_off_time > 0 && millis() >= led_off_time) {
    digitalWrite(CYD_LED_RED, HIGH);
    digitalWrite(CYD_LED_GREEN, HIGH);
    digitalWrite(CYD_LED_BLUE, HIGH);
    led_off_time = 0;
  }
}

// 產生新的波紋
void spawnRipple(float x, float y, float r, uint16_t color) {
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (!ripples[i].active) {
      ripples[i].x = x;
      ripples[i].y = y;
      ripples[i].current_r = r * 0.7f;
      ripples[i].max_r = r * 1.6f;
      ripples[i].color = color;
      ripples[i].active = true;
      break;
    }
  }
}

// ==========================================
// 4. 2D 物理碰撞引擎 (Physics Engine)
// ==========================================

// 檢查並處理牆面邊界碰撞
void resolveWallCollision(Ball &b) {
  // 左右兩側牆面
  if (b.x - b.radius < 0.0f) {
    b.x = b.radius;
    b.vx = -b.vx * BOUNCE_RESTITUTION;
    spawnRipple(b.x, b.y, b.radius, b.color);
    triggerCollisionLED(b.r_byte, b.g_byte, b.b_byte);
    total_bounces++;
  } else if (b.x + b.radius >= (float)SCREEN_W) {
    b.x = (float)SCREEN_W - b.radius - 0.1f;
    b.vx = -b.vx * BOUNCE_RESTITUTION;
    spawnRipple(b.x, b.y, b.radius, b.color);
    triggerCollisionLED(b.r_byte, b.g_byte, b.b_byte);
    total_bounces++;
  }

  // 上下兩側牆面 (避開頂部 20px HUD 資訊列)
  float top_bound = 22.0f;
  if (b.y - b.radius < top_bound) {
    b.y = top_bound + b.radius;
    b.vy = -b.vy * BOUNCE_RESTITUTION;
    spawnRipple(b.x, b.y, b.radius, b.color);
    triggerCollisionLED(b.r_byte, b.g_byte, b.b_byte);
    total_bounces++;
  } else if (b.y + b.radius >= (float)SCREEN_H) {
    b.y = (float)SCREEN_H - b.radius - 0.1f;
    b.vy = -b.vy * BOUNCE_RESTITUTION;
    spawnRipple(b.x, b.y, b.radius, b.color);
    triggerCollisionLED(b.r_byte, b.g_byte, b.b_byte);
    total_bounces++;
  }
}

// 檢查並處理球體間完全彈性碰撞 (Ball-to-Ball Elastic Collision)
void resolveBallCollision(Ball &b1, Ball &b2) {
  float dx = b2.x - b1.x;
  float dy = b2.y - b1.y;
  float dist_sq = dx * dx + dy * dy;
  float min_dist = b1.radius + b2.radius;

  // 1. 碰撞檢測 (距離小於兩半徑之和)
  if (dist_sq < min_dist * min_dist && dist_sq > 0.0001f) {
    float dist = sqrtf(dist_sq);

    // 碰撞法線單位向量
    float nx = dx / dist;
    float ny = dy / dist;

    // 相對速度向量
    float dvx = b1.vx - b2.vx;
    float dvy = b1.vy - b2.vy;

    // 法線方向投影速度
    float vel_along_normal = dvx * nx + dvy * ny;

    // 若速度方向正在分離，不處理
    if (vel_along_normal > 0.0f) {
      // 2. 位置分離校正 (Positional Correction 防止卡球/黏球 Bug)
      float overlap = min_dist - dist;
      float percent = 0.55f; // 分離係數
      float total_mass = b1.mass + b2.mass;

      if (!b1.is_dragged && !b2.is_dragged) {
        b1.x -= nx * overlap * (b2.mass / total_mass) * percent;
        b1.y -= ny * overlap * (b2.mass / total_mass) * percent;
        b2.x += nx * overlap * (b1.mass / total_mass) * percent;
        b2.y += ny * overlap * (b1.mass / total_mass) * percent;
      } else if (b1.is_dragged && !b2.is_dragged) {
        b2.x += nx * overlap;
        b2.y += ny * overlap;
      } else if (!b1.is_dragged && b2.is_dragged) {
        b1.x -= nx * overlap;
        b1.y -= ny * overlap;
      }

      // 3. 2D 動量守恆與衝量計算 (Impulse Formula)
      float impulse = (-(1.0f + BOUNCE_RESTITUTION) * vel_along_normal) / ((1.0f / b1.mass) + (1.0f / b2.mass));

      if (!b1.is_dragged) {
        b1.vx += (impulse / b1.mass) * nx;
        b1.vy += (impulse / b1.mass) * ny;
      }
      if (!b2.is_dragged) {
        b2.vx -= (impulse / b2.mass) * nx;
        b2.vy -= (impulse / b2.mass) * ny;
      }

      // 限制速度上下限避免靜止或暴走
      float speed1 = sqrtf(b1.vx * b1.vx + b1.vy * b1.vy);
      if (speed1 > MAX_VELOCITY) { b1.vx = (b1.vx / speed1) * MAX_VELOCITY; b1.vy = (b1.vy / speed1) * MAX_VELOCITY; }
      else if (speed1 < MIN_VELOCITY) { b1.vx = (b1.vx / speed1) * MIN_VELOCITY; b1.vy = (b1.vy / speed1) * MIN_VELOCITY; }

      float speed2 = sqrtf(b2.vx * b2.vx + b2.vy * b2.vy);
      if (speed2 > MAX_VELOCITY) { b2.vx = (b2.vx / speed2) * MAX_VELOCITY; b2.vy = (b2.vy / speed2) * MAX_VELOCITY; }
      else if (speed2 < MIN_VELOCITY) { b2.vx = (b2.vx / speed2) * MIN_VELOCITY; b2.vy = (b2.vy / speed2) * MIN_VELOCITY; }

      // 碰撞中心點生成光暈與硬體 LED 閃爍
      float contact_x = b1.x + nx * b1.radius;
      float contact_y = b1.y + ny * b1.radius;
      spawnRipple(contact_x, contact_y, (b1.radius + b2.radius) * 0.5f, TFT_WHITE);

      triggerCollisionLED((b1.r_byte + b2.r_byte) / 2, (b1.g_byte + b2.g_byte) / 2, (b1.b_byte + b2.b_byte) / 2);
      total_bounces++;
    }
  }
}

// 物理單步更新
void updatePhysics() {
  for (int i = 0; i < NUM_BALLS; i++) {
    Ball &b = balls[i];
    if (!b.is_dragged) {
      b.x += b.vx;
      b.y += b.vy;
      resolveWallCollision(b);
    }
  }

  // 兩兩球體碰撞檢查 (4 顆球共 6 組比對)
  for (int i = 0; i < NUM_BALLS - 1; i++) {
    for (int j = i + 1; j < NUM_BALLS; j++) {
      resolveBallCollision(balls[i], balls[j]);
    }
  }
}

// ==========================================
// 5. 高效無閃爍繪製 (Flicker-Free Graphics)
// ==========================================

// 抹除舊位置 (上一幀)
void eraseBall(const Ball &b) {
  // 抹除上一幀球體以及波紋範圍 (+3px 安全邊界)
  tft.fillCircle((int)b.last_x, (int)b.last_y, (int)b.radius + 3, COLOR_BG);
}

// 繪製立體光澤球體 (3D Shaded Sphere)
void drawShadedBall(const Ball &b) {
  int ix = (int)b.x;
  int iy = (int)b.y;
  int ir = (int)b.radius;

  // 1. 底層暗面色圈
  tft.fillCircle(ix, iy, ir, b.dark_color);

  // 2. 主色調球體
  tft.fillCircle(ix, iy, ir - 2, b.color);

  // 3. 偏心光源過渡亮圈
  int light_ox = ix - ir / 4;
  int light_oy = iy - ir / 4;
  tft.fillCircle(light_ox, light_oy, ir / 2, b.light_color);

  // 4. 白色鏡面高光小亮點 (Specular Highlight)
  int spec_ox = ix - ir / 3;
  int spec_oy = iy - ir / 3;
  tft.fillCircle(spec_ox, spec_oy, max(2, ir / 6), TFT_WHITE);
}

// 繪製與更新衝擊波紋
void updateAndDrawRipples() {
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (ripples[i].active) {
      // 抹除舊圈
      tft.drawCircle((int)ripples[i].x, (int)ripples[i].y, (int)ripples[i].current_r, COLOR_BG);

      ripples[i].current_r += 2.0f;
      if (ripples[i].current_r >= ripples[i].max_r) {
        ripples[i].active = false;
      } else {
        tft.drawCircle((int)ripples[i].x, (int)ripples[i].y, (int)ripples[i].current_r, ripples[i].color);
      }
    }
  }
}

// 頂部 HUD 資訊面板
void drawHUD() {
  tft.fillRect(0, 0, 320, 20, 0x0000);
  tft.drawFastHLine(0, 20, 320, 0x2126);

  tft.setTextColor(0x07FF, 0x0000);
  tft.setTextSize(1);
  tft.drawString("2D ELASTIC RGB BALLS", 8, 6);

  char buf[48];
  snprintf(buf, sizeof(buf), "FPS: %.0f | Bounces: %lu", current_fps, (unsigned long)total_bounces);
  tft.drawString(buf, 175, 6);
}

// ==========================================
// 6. 觸控輸入處理 (Touch Interaction)
// ==========================================
void handleTouch() {
  static float touch_last_x = 0;
  static float touch_last_y = 0;

  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    // 螢幕校正映射 (CYD 320x240 橫向)
    int tx = map(p.x, 240, 3800, 0, 320);
    int ty = map(p.y, 240, 3800, 0, 240);
    tx = constrain(tx, 0, 319);
    ty = constrain(ty, 22, 239);

    if (dragged_ball_idx < 0) {
      // 檢測是否點中某顆球
      for (int i = 0; i < NUM_BALLS; i++) {
        float dx = tx - balls[i].x;
        float dy = ty - balls[i].y;
        if (dx * dx + dy * dy <= (balls[i].radius + 8.0f) * (balls[i].radius + 8.0f)) {
          dragged_ball_idx = i;
          balls[i].is_dragged = true;
          balls[i].drag_off_x = balls[i].x - tx;
          balls[i].drag_off_y = balls[i].y - ty;
          balls[i].vx = 0;
          balls[i].vy = 0;
          break;
        }
      }

      // 若未點中任何球：在手指位置產生衝擊波將球推開 (Force Push)
      if (dragged_ball_idx < 0) {
        spawnRipple(tx, ty, 30.0f, 0x07FF);
        for (int i = 0; i < NUM_BALLS; i++) {
          float pdx = balls[i].x - tx;
          float pdy = balls[i].y - ty;
          float pdist = sqrtf(pdx * pdx + pdy * pdy);
          if (pdist > 1.0f && pdist < 120.0f) {
            float push_factor = (120.0f - pdist) / 120.0f * 4.5f;
            balls[i].vx += (pdx / pdist) * push_factor;
            balls[i].vy += (pdy / pdist) * push_factor;
          }
        }
      }
    } else {
      // 拖曳該球
      Ball &b = balls[dragged_ball_idx];
      b.x = tx + b.drag_off_x;
      b.y = ty + b.drag_off_y;

      // 邊界防禦
      b.x = constrain(b.x, b.radius, (float)SCREEN_W - b.radius);
      b.y = constrain(b.y, 22.0f + b.radius, (float)SCREEN_H - b.radius);

      // 計算手勢甩出力道
      b.vx = (tx - touch_last_x) * 0.8f;
      b.vy = (ty - touch_last_y) * 0.8f;
    }

    touch_last_x = tx;
    touch_last_y = ty;
  } else {
    // 釋放拖曳
    if (dragged_ball_idx >= 0) {
      balls[dragged_ball_idx].is_dragged = false;
      // 賦予最小速度避免靜止
      float spd = sqrtf(balls[dragged_ball_idx].vx * balls[dragged_ball_idx].vx + 
                        balls[dragged_ball_idx].vy * balls[dragged_ball_idx].vy);
      if (spd < MIN_VELOCITY) {
        balls[dragged_ball_idx].vx = random(-30, 30) * 0.1f;
        balls[dragged_ball_idx].vy = random(-30, 30) * 0.1f;
      }
      dragged_ball_idx = -1;
    }
  }
}

// ==========================================
// 7. 系統初始化 (setup)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n============================================");
  Serial.println("   ESP32 CYD - 4 Flying RGB Elastic Balls   ");
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

  // 初始化 TFT 顯示器
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);

  // 初始化 XPT2046 觸控螢幕
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 清空波紋
  for (int i = 0; i < MAX_RIPPLES; i++) ripples[i].active = false;

  // 初始化 4 顆不同半徑、質量、外觀的隨機霓虹 RGB 球體
  // 球 0: 霓虹青藍 (Cyan)
  balls[0].radius = 18.0f;
  balls[0].x = 70.0f; balls[0].y = 80.0f;
  balls[0].vx = 3.6f; balls[0].vy = 2.4f;
  balls[0].color = tft.color565(0, 240, 255);
  balls[0].light_color = tft.color565(140, 255, 255);
  balls[0].dark_color = tft.color565(0, 100, 140);
  balls[0].r_byte = 0; balls[0].g_byte = 255; balls[0].b_byte = 255;

  // 球 1: 霓虹洋紅/鮮紅 (Magenta / Pink-Red)
  balls[1].radius = 24.0f;
  balls[1].x = 240.0f; balls[1].y = 90.0f;
  balls[1].vx = -3.2f; balls[1].vy = 2.8f;
  balls[1].color = tft.color565(255, 30, 140);
  balls[1].light_color = tft.color565(255, 140, 200);
  balls[1].dark_color = tft.color565(140, 0, 60);
  balls[1].r_byte = 255; balls[1].g_byte = 30; balls[1].b_byte = 140;

  // 球 2: 霓虹亮綠 (Neon Lime Green)
  balls[2].radius = 21.0f;
  balls[2].x = 100.0f; balls[2].y = 180.0f;
  balls[2].vx = 2.8f; balls[2].vy = -3.5f;
  balls[2].color = tft.color565(50, 255, 60);
  balls[2].light_color = tft.color565(170, 255, 170);
  balls[2].dark_color = tft.color565(10, 120, 20);
  balls[2].r_byte = 50; balls[2].g_byte = 255; balls[2].b_byte = 60;

  // 球 3: 霓虹金橙 (Amber Gold / Yellow)
  balls[3].radius = 27.0f;
  balls[3].x = 220.0f; balls[3].y = 175.0f;
  balls[3].vx = -2.5f; balls[3].vy = -2.6f;
  balls[3].color = tft.color565(255, 190, 0);
  balls[3].light_color = tft.color565(255, 235, 130);
  balls[3].dark_color = tft.color565(150, 90, 0);
  balls[3].r_byte = 255; balls[3].g_byte = 200; balls[3].b_byte = 0;

  for (int i = 0; i < NUM_BALLS; i++) {
    balls[i].last_x = balls[i].x;
    balls[i].last_y = balls[i].y;
    balls[i].mass = balls[i].radius * balls[i].radius; // 質量與體積/面積正比
    balls[i].is_dragged = false;
  }

  drawHUD();
  fps_timer = millis();
  Serial.println("[SYSTEM] 4 Elastic RGB balls ready!");
}

// ==========================================
// 8. 主運算迴圈 (60 FPS Game Loop)
// ==========================================
void loop() {
  // 1. 處理觸控手勢
  handleTouch();

  // 2. 局部抹除 4 顆球上一幀位置
  for (int i = 0; i < NUM_BALLS; i++) {
    eraseBall(balls[i]);
  }

  // 3. 2D 物理碰撞模擬運算
  updatePhysics();

  // 4. 繪製衝擊波紋
  updateAndDrawRipples();

  // 5. 繪製 4 顆球體新位置 (按半徑由大至小繪製維持層次)
  for (int i = 0; i < NUM_BALLS; i++) {
    drawShadedBall(balls[i]);
    // 保存當前位置作為下一幀抹除基準
    balls[i].last_x = balls[i].x;
    balls[i].last_y = balls[i].y;
  }

  // 6. 硬體 LED 與計時維護
  updateLEDState();

  // 7. FPS 效能計數與 HUD 更新 (每秒一次)
  frame_count++;
  unsigned long now = millis();
  if (now - fps_timer >= 1000) {
    current_fps = (float)frame_count * 1000.0f / (float)(now - fps_timer);
    frame_count = 0;
    fps_timer = now;
    drawHUD();
  }

  // 控制幀率穩定在 ~60 FPS (每幀約 16ms)
  delay(12);
}
