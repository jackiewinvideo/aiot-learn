#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ==========================================
// 1. 硬體周邊與腳位定義
// ==========================================
TFT_eSPI tft = TFT_eSPI();

// CYD 專屬 XPT2046 觸控 SPI 腳位
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

#define CYD_LDR_PIN 34    // 板載光敏電阻
#define SEABED_Y 208      // 沙灘頂部 Y 座標

// ==========================================
// 2. 顏色定義與背景漸層
// ==========================================
#define COLOR_SAND       0xDE90  // 金黃細沙
#define COLOR_SAND_DARK  0xAD48  // 深色沙紋
#define COLOR_SEAWEED    0x2E64  // 海草綠
#define COLOR_SEAWEED_LT 0x56E9  // 亮綠海草
#define COLOR_FOOD       0xB362  // 飼料褐色
#define COLOR_HEART      0xF9BE  // 愛心粉紅

// 取得特定 Y 高度的海水顏色 (頂部淡青藍 -> 底部深藍海)
inline uint16_t getWaterColor(int y, bool is_night) {
  if (y >= SEABED_Y) return COLOR_SAND;
  if (is_night) {
    // 夜間深海深藍
    uint8_t g = map(y, 0, SEABED_Y, 20, 5);
    uint8_t b = map(y, 0, SEABED_Y, 70, 25);
    return tft.color565(0, g, b);
  } else {
    // 日間清澈熱帶海
    uint8_t g = map(y, 0, SEABED_Y, 150, 45);
    uint8_t b = map(y, 0, SEABED_Y, 235, 105);
    return tft.color565(0, g, b);
  }
}

// 快速清除動態物件佔據的水域區域（避免全螢幕重繪造成的劇烈閃爍）
void eraseRect(int x, int y, int w, int h, bool is_night) {
  for (int j = y; j < y + h; j++) {
    if (j >= 0 && j < 240) {
      int rx = max(0, x);
      int rw = min(320 - rx, w);
      if (rw > 0) {
        tft.drawFastHLine(rx, j, rw, getWaterColor(j, is_night));
      }
    }
  }
}

// ==========================================
// 3. 魚飼料結構 (Pellet)
// ==========================================
#define MAX_FOOD 12
struct Food {
  float x, y;
  bool active;
  unsigned long drop_time;
};
Food foods[MAX_FOOD];

void spawnFood(int x, int y) {
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!foods[i].active) {
      foods[i].x = x;
      foods[i].y = y;
      foods[i].active = true;
      foods[i].drop_time = millis();
      break;
    }
  }
}

// ==========================================
// 4. 氣泡結構 (Bubbles)
// ==========================================
#define MAX_BUBBLES 12
struct Bubble {
  float x, y;
  float speed;
  int size;
  float sway_offset;
};
Bubble bubbles[MAX_BUBBLES];

void initBubbles() {
  for (int i = 0; i < MAX_BUBBLES; i++) {
    bubbles[i].x = random(20, 300);
    bubbles[i].y = random(20, SEABED_Y);
    bubbles[i].speed = random(8, 22) / 10.0f;
    bubbles[i].size = random(1, 4);
    bubbles[i].sway_offset = random(0, 100);
  }
}

// ==========================================
// 5. 10 隻不同種類的像素魚結構與屬性
// ==========================================
enum FishType {
  FISH_CLOWN = 0,    // 0: 小丑魚 (尼莫 Nemo)
  FISH_BLUE_TANG,    // 1: 藍倒吊 (多莉 Dory)
  FISH_ANGEL,        // 2: 橫紋神仙魚
  FISH_PUFFER,       // 3: 刺豚 / 河豚
  FISH_GOLDFISH,     // 4: 琉金金魚
  FISH_NEON_TETRA,   // 5: 霓虹燈魚
  FISH_ZEBRA,        // 6: 斑馬魚
  FISH_SHARK,        // 7: 礁鯊 / 小青鯊
  FISH_TURTLE,       // 8: 綠海龜
  FISH_JELLYFISH     // 9: 螢光粉水母
};

struct Fish {
  FishType type;
  const char* name;
  float x, y;
  float vx, vy;
  int dir;           // 1: 向右, -1: 向左
  int w, h;          // 碰撞與繪圖尺寸
  int anim_tick;
  int frame;
  int old_x, old_y;
  int old_w, old_h;
  bool is_puffed;    // 河豚專用膨脹狀態
  int love_timer;    // 吃飽愛心計時器
};

#define TOTAL_FISH 10
Fish school[TOTAL_FISH];

void initFishSchool() {
  // 10 隻魚的初始設定 (種類、名稱、速度範圍、繪圖寬高)
  struct FishConfig {
    FishType t;
    const char* name;
    int w, h;
    float max_v;
  } configs[TOTAL_FISH] = {
    {FISH_CLOWN,      "Clownfish",  18, 11, 1.4f},
    {FISH_BLUE_TANG,  "Blue Tang",  20, 12, 1.6f},
    {FISH_ANGEL,      "Angelfish",  16, 18, 1.2f},
    {FISH_PUFFER,     "Pufferfish", 16, 14, 0.9f},
    {FISH_GOLDFISH,   "Goldfish",   20, 14, 1.3f},
    {FISH_NEON_TETRA, "Neon Tetra", 13,  7, 2.0f},
    {FISH_ZEBRA,      "Zebra Fish", 15,  8, 1.8f},
    {FISH_SHARK,      "Reef Shark", 26, 12, 1.7f},
    {FISH_TURTLE,     "Sea Turtle", 20, 16, 0.8f},
    {FISH_JELLYFISH,  "Jellyfish",  14, 16, 0.6f}
  };

  for (int i = 0; i < TOTAL_FISH; i++) {
    school[i].type = configs[i].t;
    school[i].name = configs[i].name;
    school[i].w = configs[i].w;
    school[i].h = configs[i].h;
    school[i].x = random(30, 280);
    school[i].y = random(25, SEABED_Y - 30);
    school[i].dir = random(0, 2) == 0 ? 1 : -1;
    school[i].vx = (random(8, 18) / 10.0f) * school[i].dir;
    school[i].vy = (random(-4, 5) / 10.0f);
    school[i].anim_tick = random(0, 10);
    school[i].frame = 0;
    school[i].old_x = school[i].x;
    school[i].old_y = school[i].y;
    school[i].old_w = school[i].w;
    school[i].old_h = school[i].h;
    school[i].is_puffed = false;
    school[i].love_timer = 0;
  }
}

// ==========================================
// 6. 10 隻像素魚獨立繪圖引擎
// ==========================================

// 輔助函式：根據朝向繪製水平鏡像像素
inline void drawPixelDir(int ox, int oy, int dx, int dy, int dir, int w, uint16_t color) {
  int px = (dir == 1) ? (ox + dx) : (ox + w - 1 - dx);
  int py = oy + dy;
  if (px >= 0 && px < 320 && py >= 0 && py < 240) {
    tft.drawPixel(px, py, color);
  }
}

void drawFish(Fish &f, bool is_night) {
  int ox = (int)f.x;
  int oy = (int)f.y;
  int d = f.dir;
  int frm = f.frame;

  switch (f.type) {
    // 0. 小丑魚 (橙色主體 + 2道白色斑紋 + 黑邊 + 小黑眼)
    case FISH_CLOWN: {
      uint16_t body = is_night ? 0xC2E0 : TFT_ORANGE;
      // 身體輪廓 (18x11)
      for (int dy = 2; dy <= 8; dy++) {
        for (int dx = 3; dx <= 13; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, body);
        }
      }
      // 白條紋 1 (頭部後方)
      for (int dy = 2; dy <= 8; dy++) {
        drawPixelDir(ox, oy, 7, dy, d, f.w, TFT_WHITE);
        drawPixelDir(ox, oy, 8, dy, d, f.w, TFT_WHITE);
      }
      // 白條紋 2 (尾柄前)
      for (int dy = 3; dy <= 7; dy++) {
        drawPixelDir(ox, oy, 12, dy, d, f.w, TFT_WHITE);
      }
      // 眼睛
      drawPixelDir(ox, oy, 14, 4, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 15, 3, d, f.w, TFT_WHITE);
      // 尾鰭擺動 (frm 0 / 1)
      int tail_y = (frm == 0) ? 5 : 6;
      drawPixelDir(ox, oy, 1, tail_y - 2, d, f.w, body);
      drawPixelDir(ox, oy, 0, tail_y - 1, d, f.w, body);
      drawPixelDir(ox, oy, 0, tail_y,     d, f.w, body);
      drawPixelDir(ox, oy, 0, tail_y + 1, d, f.w, body);
      drawPixelDir(ox, oy, 1, tail_y + 2, d, f.w, body);
      break;
    }

    // 1. 藍倒吊 (亮寶藍身 + 尾部黑環紋 + 亮黃三角尾)
    case FISH_BLUE_TANG: {
      uint16_t blue = is_night ? 0x011B : 0x1A7F;
      // 身體 (20x12)
      for (int dy = 2; dy <= 9; dy++) {
        for (int dx = 4; dx <= 15; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, blue);
        }
      }
      // 黑色弧形側線紋
      drawPixelDir(ox, oy, 9, 3, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 8, 4, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 7, 5, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 7, 6, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 8, 7, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 10, 7, d, f.w, TFT_BLACK);
      // 亮黃色尾鰭
      int ty = (frm == 0) ? 5 : 6;
      for (int k = -3; k <= 3; k++) {
        drawPixelDir(ox, oy, 1, ty + k, d, f.w, TFT_YELLOW);
        if (abs(k) <= 1) drawPixelDir(ox, oy, 2, ty + k, d, f.w, TFT_YELLOW);
      }
      // 眼睛
      drawPixelDir(ox, oy, 16, 4, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 17, 4, d, f.w, TFT_WHITE);
      break;
    }

    // 2. 神仙魚 (高身菱形 + 上下長鰭 + 黑黃斑紋)
    case FISH_ANGEL: {
      uint16_t gold = is_night ? 0x9480 : TFT_YELLOW;
      // 菱形軀幹 (16x18)
      for (int dy = 5; dy <= 12; dy++) {
        int w_span = 5 - abs(dy - 8);
        for (int dx = 7 - w_span; dx <= 7 + w_span; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, gold);
        }
      }
      // 垂直黑色縱斑
      for (int dy = 6; dy <= 11; dy++) {
        drawPixelDir(ox, oy, 7, dy, d, f.w, TFT_BLACK);
      }
      // 向上長背鰭
      drawPixelDir(ox, oy, 6, 4, d, f.w, TFT_WHITE);
      drawPixelDir(ox, oy, 5, 3, d, f.w, TFT_WHITE);
      drawPixelDir(ox, oy, 4, 1, d, f.w, TFT_WHITE);
      // 向下長腹鰭 (優雅長鬚)
      drawPixelDir(ox, oy, 7, 13, d, f.w, TFT_WHITE);
      drawPixelDir(ox, oy, 6, 15, d, f.w, TFT_WHITE);
      drawPixelDir(ox, oy, 5, 17, d, f.w, TFT_WHITE);
      // 眼睛
      drawPixelDir(ox, oy, 11, 8, d, f.w, TFT_BLACK);
      break;
    }

    // 3. 刺豚 / 河豚 (胖圓球淺黃 + 散落小刺 + 大眼)
    case FISH_PUFFER: {
      uint16_t body = is_night ? 0xB5A0 : 0xFFE0;
      int r = f.is_puffed ? 7 : 5;
      int cx = 8, cy = 7;
      // 圓胖身形
      for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
          if (dx * dx + dy * dy <= r * r) {
            drawPixelDir(ox, oy, cx + dx, cy + dy, d, f.w, body);
          }
        }
      }
      // 眼睛
      drawPixelDir(ox, oy, cx + r - 2, cy - 2, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, cx + r - 1, cy - 3, d, f.w, TFT_WHITE);
      // 膨脹時的小尖刺
      if (f.is_puffed) {
        drawPixelDir(ox, oy, cx, cy - r - 1, d, f.w, TFT_BROWN);
        drawPixelDir(ox, oy, cx, cy + r + 1, d, f.w, TFT_BROWN);
        drawPixelDir(ox, oy, cx - r - 1, cy, d, f.w, TFT_BROWN);
      }
      // 小扇尾
      drawPixelDir(ox, oy, cx - r, cy, d, f.w, 0xFDA0);
      drawPixelDir(ox, oy, cx - r - 1, cy + (frm == 0 ? 1 : -1), d, f.w, 0xFDA0);
      break;
    }

    // 4. 琉金金魚 (鮮紅圓腹 + 波浪狀透明大尾鰭)
    case FISH_GOLDFISH: {
      uint16_t red = is_night ? 0xB000 : 0xF940;
      // 圓潤肚子
      for (int dy = 3; dy <= 10; dy++) {
        for (int dx = 5; dx <= 14; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, red);
        }
      }
      // 眼睛
      drawPixelDir(ox, oy, 14, 5, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 15, 4, d, f.w, TFT_WHITE);
      // 波浪大尾鰭 (frm 動畫)
      uint16_t fin = is_night ? 0xD2A0 : 0xFD80;
      int tw = (frm == 0) ? 0 : 1;
      drawPixelDir(ox, oy, 4, 6, d, f.w, fin);
      drawPixelDir(ox, oy, 3, 5 + tw, d, f.w, fin);
      drawPixelDir(ox, oy, 2, 4 + tw, d, f.w, fin);
      drawPixelDir(ox, oy, 1, 3 + tw, d, f.w, fin);
      drawPixelDir(ox, oy, 2, 8 - tw, d, f.w, fin);
      drawPixelDir(ox, oy, 1, 9 - tw, d, f.w, fin);
      break;
    }

    // 5. 霓虹燈魚 (細小銀身 + 藍青發光側線 + 鮮紅尾線)
    case FISH_NEON_TETRA: {
      // 銀白腹部
      for (int dx = 2; dx <= 9; dx++) {
        drawPixelDir(ox, oy, dx, 4, d, f.w, TFT_LIGHTGREY);
      }
      // 上半身螢光亮青線
      for (int dx = 3; dx <= 10; dx++) {
        drawPixelDir(ox, oy, dx, 2, d, f.w, TFT_CYAN);
      }
      // 下半身鮮紅尾線
      for (int dx = 2; dx <= 6; dx++) {
        drawPixelDir(ox, oy, dx, 3, d, f.w, TFT_RED);
      }
      // 黑眼
      drawPixelDir(ox, oy, 10, 3, d, f.w, TFT_BLACK);
      // 尾尖透明鰭
      drawPixelDir(ox, oy, 0, 3 + frm, d, f.w, TFT_WHITE);
      break;
    }

    // 6. 斑馬魚 (修長銀底 + 水平深藍紋)
    case FISH_ZEBRA: {
      uint16_t silver = 0xDEDB;
      // 身體
      for (int dy = 2; dy <= 5; dy++) {
        for (int dx = 2; dx <= 11; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, silver);
        }
      }
      // 3 條水平深藍斑紋
      for (int dx = 3; dx <= 10; dx++) {
        drawPixelDir(ox, oy, dx, 2, d, f.w, 0x0017);
        drawPixelDir(ox, oy, dx, 4, d, f.w, 0x0017);
      }
      // 眼睛
      drawPixelDir(ox, oy, 11, 3, d, f.w, TFT_BLACK);
      drawPixelDir(ox, oy, 0, 3 + frm, d, f.w, silver);
      break;
    }

    // 7. 礁鯊 / 小青鯊 (流線灰藍身 + 三角高背鰭 + 白肚皮)
    case FISH_SHARK: {
      uint16_t gray = is_night ? 0x4228 : 0x73B2;
      // 灰藍色背部
      for (int dy = 3; dy <= 6; dy++) {
        for (int dx = 5; dx <= 20; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, gray);
        }
      }
      // 白色肚皮
      for (int dx = 8; dx <= 18; dx++) {
        drawPixelDir(ox, oy, dx, 7, d, f.w, TFT_WHITE);
      }
      // 招牌三角背鰭
      drawPixelDir(ox, oy, 12, 2, d, f.w, gray);
      drawPixelDir(ox, oy, 13, 1, d, f.w, gray);
      drawPixelDir(ox, oy, 14, 0, d, f.w, gray);
      // 鯊魚異型大尾鰭
      drawPixelDir(ox, oy, 3, 5, d, f.w, gray);
      drawPixelDir(ox, oy, 2, 4, d, f.w, gray);
      drawPixelDir(ox, oy, 1, 2 + frm, d, f.w, gray);
      drawPixelDir(ox, oy, 0, 1 + frm, d, f.w, gray);
      drawPixelDir(ox, oy, 2, 7, d, f.w, gray);
      drawPixelDir(ox, oy, 1, 8, d, f.w, gray);
      // 黑眼
      drawPixelDir(ox, oy, 20, 4, d, f.w, TFT_BLACK);
      break;
    }

    // 8. 綠海龜 (綠龜殼 + 棋盤盾紋 + 四肢划動)
    case FISH_TURTLE: {
      uint16_t shell = is_night ? 0x2280 : 0x3444;
      uint16_t skin  = is_night ? 0x2B40 : 0x5668;
      // 圓形龜殼
      for (int dy = 4; dy <= 12; dy++) {
        for (int dx = 5; dx <= 14; dx++) {
          drawPixelDir(ox, oy, dx, dy, d, f.w, shell);
        }
      }
      // 龜殼紋理
      drawPixelDir(ox, oy, 8, 7, d, f.w, 0x1A00);
      drawPixelDir(ox, oy, 11, 7, d, f.w, 0x1A00);
      drawPixelDir(ox, oy, 9, 9, d, f.w, 0x1A00);
      // 頭部
      drawPixelDir(ox, oy, 16, 7, d, f.w, skin);
      drawPixelDir(ox, oy, 17, 8, d, f.w, skin);
      drawPixelDir(ox, oy, 17, 7, d, f.w, TFT_BLACK); // 眼睛
      // 划水前肢 (動態)
      int paddle = (frm == 0) ? -2 : 2;
      drawPixelDir(ox, oy, 12, 3 + paddle, d, f.w, skin);
      drawPixelDir(ox, oy, 13, 2 + paddle, d, f.w, skin);
      drawPixelDir(ox, oy, 12, 13 - paddle, d, f.w, skin);
      drawPixelDir(ox, oy, 13, 14 - paddle, d, f.w, skin);
      // 後肢
      drawPixelDir(ox, oy, 4, 5, d, f.w, skin);
      drawPixelDir(ox, oy, 4, 11, d, f.w, skin);
      break;
    }

    // 9. 夢幻粉水母 (半圓傘蓋 + 飄逸觸手)
    case FISH_JELLYFISH: {
      uint16_t pink = is_night ? 0xFA1F : 0xFC18;
      uint16_t glow = is_night ? 0xFFFF : 0xFD5F;
      // 頂部圓傘蓋 (14x16)
      for (int dx = 2; dx <= 11; dx++) {
        drawPixelDir(ox, oy, dx, 2, 1, f.w, glow);
        drawPixelDir(ox, oy, dx, 3, 1, f.w, pink);
        drawPixelDir(ox, oy, dx, 4, 1, f.w, pink);
      }
      drawPixelDir(ox, oy, 1, 4, 1, f.w, pink);
      drawPixelDir(ox, oy, 12, 4, 1, f.w, pink);
      drawPixelDir(ox, oy, 3, 1, 1, f.w, glow);
      drawPixelDir(ox, oy, 10, 1, 1, f.w, glow);

      // 下垂觸手 (波動擺動)
      int s0 = (frm == 0) ? 0 : 1;
      int s1 = (frm == 0) ? 1 : 0;
      for (int ty = 5; ty <= 14; ty++) {
        drawPixelDir(ox, oy, 3 + s0, ty, 1, f.w, pink);
        drawPixelDir(ox, oy, 6 + s1, ty, 1, f.w, glow);
        drawPixelDir(ox, oy, 8 + s0, ty, 1, f.w, glow);
        drawPixelDir(ox, oy, 10 + s1, ty, 1, f.w, pink);
      }
      break;
    }
  }

  // 吃飽愛心特效
  if (f.love_timer > 0) {
    tft.drawPixel(ox + 8, oy - 4, COLOR_HEART);
    tft.drawPixel(ox + 6, oy - 6, COLOR_HEART);
    tft.drawPixel(ox + 7, oy - 6, COLOR_HEART);
    tft.drawPixel(ox + 9, oy - 6, COLOR_HEART);
    tft.drawPixel(ox + 10, oy - 6, COLOR_HEART);
    tft.drawPixel(ox + 8, oy - 5, COLOR_HEART);
  }
}

// ==========================================
// 7. 水族箱靜態背景與造景 (沙灘、海草、彩石)
// ==========================================
float seaweed_angle = 0.0f;

void drawSeaweedBranch(int base_x, int base_y, int height, float phase) {
  int prev_x = base_x;
  int prev_y = base_y;
  for (int h = 0; h <= height; h += 6) {
    float sway = sin(seaweed_angle + phase + h * 0.06f) * (h * 0.12f);
    int curr_x = base_x + (int)sway;
    int curr_y = base_y - h;
    tft.drawLine(prev_x, prev_y, curr_x, curr_y, (h % 12 == 0) ? COLOR_SEAWEED_LT : COLOR_SEAWEED);
    prev_x = curr_x;
    prev_y = curr_y;
  }
}

void drawAquariumEnvironment(bool is_night) {
  // 1. 海水漸層
  for (int y = 0; y < SEABED_Y; y++) {
    tft.drawFastHLine(0, y, 320, getWaterColor(y, is_night));
  }

  // 頂部陽光波光特效 (Caustics shimmer)
  if (!is_night) {
    for (int x = 0; x < 320; x += 32) {
      tft.drawFastHLine(x + 4, 3, 16, 0x5FFF);
      tft.drawFastHLine(x + 12, 7, 12, 0x3EBF);
    }
  }

  // 2. 底部沙灘
  tft.fillRect(0, SEABED_Y, 320, 240 - SEABED_Y, COLOR_SAND);
  tft.drawFastHLine(0, SEABED_Y, 320, COLOR_SAND_DARK);

  // 沙粒點綴
  for (int x = 8; x < 312; x += 18) {
    tft.drawPixel(x, SEABED_Y + 4, COLOR_SAND_DARK);
    tft.drawPixel(x + 6, SEABED_Y + 12, COLOR_SAND_DARK);
    tft.drawPixel(x + 11, SEABED_Y + 20, 0x8A00);
  }

  // 彩色珊瑚碎石與小海星
  tft.fillRect(80, SEABED_Y + 8, 8, 4, 0x94B2); // 灰色鵝卵石
  tft.fillRect(210, SEABED_Y + 10, 6, 4, 0xCE59); // 貝殼粉白
  // 小紅海星 (Starfish)
  tft.drawPixel(140, SEABED_Y + 12, TFT_RED);
  tft.drawPixel(141, SEABED_Y + 12, TFT_RED);
  tft.drawPixel(140, SEABED_Y + 11, TFT_RED);
  tft.drawPixel(140, SEABED_Y + 13, TFT_RED);
  tft.drawPixel(139, SEABED_Y + 12, TFT_RED);
  tft.drawPixel(142, SEABED_Y + 12, TFT_RED);
}

// ==========================================
// 8. 系統初始化 (setup)
// ==========================================
bool is_night_mode = false;
int fed_count = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32 CYD Pixel Aquarium Initializing ===");

  // 初始化板載 RGB LED (Active LOW)
  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);
  digitalWrite(CYD_LED_RED, HIGH);
  digitalWrite(CYD_LED_GREEN, HIGH);
  digitalWrite(CYD_LED_BLUE, HIGH);

  // 初始化 TFT 螢幕 (橫向模式 320x240)
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // 初始化 XPT2046 觸控螢幕
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // 初始化魚群與氣泡
  initFishSchool();
  initBubbles();

  // 繪製初始水族箱背景
  drawAquariumEnvironment(is_night_mode);

  Serial.println("[AQUARIUM] 10 Species of Pixel Fish Ready!");
}

// ==========================================
// 9. 主迴圈 (動畫、魚群 AI、觸控投餵)
// ==========================================
unsigned long last_anim_update = 0;
unsigned long last_seaweed_update = 0;
unsigned long last_ldr_check = 0;

void loop() {
  unsigned long now = millis();

  // 1. 日夜環境光感測 (利用 CYD 板載光敏電阻 LDR - GPIO 34)
  if (now - last_ldr_check > 2000) {
    int light_val = analogRead(CYD_LDR_PIN);
    bool should_be_night = (light_val > 3000); // 光線極暗時切換夜間模式
    if (should_be_night != is_night_mode) {
      is_night_mode = should_be_night;
      drawAquariumEnvironment(is_night_mode);
      // 夜間開啟板載微光藍色小夜燈
      digitalWrite(CYD_LED_BLUE, is_night_mode ? LOW : HIGH);
    }
    last_ldr_check = now;
  }

  // 2. 觸控投餵魚飼料 (點擊螢幕任何位置生成魚飼料)
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int tx = map(p.x, 240, 3800, 0, 320);
    int ty = map(p.y, 240, 3800, 0, 240);

    if (ty < SEABED_Y) {
      spawnFood(tx, ty);
      fed_count++;

      // 觸控音效/LED 短暫微光回饋
      digitalWrite(CYD_LED_GREEN, LOW);
      delay(30);
      digitalWrite(CYD_LED_GREEN, HIGH);
    }
  }

  // 3. 更新與繪製魚飼料 (Pellets)
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foods[i].active) {
      // 抹除上一幀飼料
      tft.fillRect((int)foods[i].x - 1, (int)foods[i].y - 1, 3, 3, getWaterColor((int)foods[i].y, is_night_mode));

      // 飼料緩慢下沉
      foods[i].y += 0.7f;
      foods[i].x += sin(now * 0.005f + i) * 0.2f;

      // 落在沙灘上 4 秒後消失
      if (foods[i].y >= SEABED_Y - 2) {
        if (now - foods[i].drop_time > 4000) {
          foods[i].active = false;
        }
      }

      // 繪製新飼料
      if (foods[i].active && foods[i].y < SEABED_Y) {
        tft.fillRect((int)foods[i].x, (int)foods[i].y, 2, 2, COLOR_FOOD);
      }
    }
  }

  // 4. 更新與繪製氣泡 (Bubbles)
  for (int i = 0; i < MAX_BUBBLES; i++) {
    // 抹除舊氣泡
    tft.drawCircle((int)bubbles[i].x, (int)bubbles[i].y, bubbles[i].size, getWaterColor((int)bubbles[i].y, is_night_mode));

    // 氣泡往上升
    bubbles[i].y -= bubbles[i].speed;
    bubbles[i].x += sin(now * 0.004f + bubbles[i].sway_offset) * 0.3f;

    // 升到頂部破裂重生
    if (bubbles[i].y <= 6) {
      bubbles[i].y = SEABED_Y - random(2, 10);
      bubbles[i].x = random(20, 300);
    }

    // 繪製新氣泡
    if (bubbles[i].y < SEABED_Y) {
      uint16_t b_color = is_night_mode ? 0x9E7F : 0xDFFF;
      tft.drawCircle((int)bubbles[i].x, (int)bubbles[i].y, bubbles[i].size, b_color);
    }
  }

  // 5. 更新 10 隻魚的游動行為與 AI (尋食、漫遊、邊界轉向)
  for (int i = 0; i < TOTAL_FISH; i++) {
    Fish &f = school[i];

    // (A) 抹除魚在上一幀的位置 (局部抹除，零閃爍)
    eraseRect(f.old_x - 2, f.old_y - 2, f.old_w + 14, f.old_h + 10, is_night_mode);

    // (B) 尋找最近的魚飼料 AI
    int nearest_food = -1;
    float min_dist = 90.0f;
    for (int k = 0; k < MAX_FOOD; k++) {
      if (foods[k].active && foods[k].y < SEABED_Y) {
        float d = hypot(foods[k].x - f.x, foods[k].y - f.y);
        if (d < min_dist) {
          min_dist = d;
          nearest_food = k;
        }
      }
    }

    if (nearest_food != -1) {
      // 被飼料吸引，加速游向飼料
      float target_dx = foods[nearest_food].x - f.x;
      float target_dy = foods[nearest_food].y - f.y;
      f.vx = (target_dx > 0 ? 1.5f : -1.5f);
      f.vy = (target_dy > 0 ? 0.8f : -0.8f);
      f.dir = (target_dx > 0) ? 1 : -1;

      // 距離很近吃掉飼料！
      if (min_dist < 8.0f) {
        foods[nearest_food].active = false;
        f.love_timer = 40; // 顯示愛心
        if (f.type == FISH_PUFFER) f.is_puffed = true; // 河豚吃飽膨脹！
      }
    } else {
      // 正常自由漫遊，偶爾隨機微調 Y 軸游速
      if (random(0, 45) == 0) {
        f.vy = (random(-8, 9) / 10.0f);
      }
      if (random(0, 120) == 0) {
        f.dir = -f.dir;
        f.vx = f.dir * (random(8, 16) / 10.0f);
      }
    }

    // 水母特殊垂直浮游運動
    if (f.type == FISH_JELLYFISH) {
      f.vx = sin(now * 0.003f + i) * 0.4f;
      f.vy = -0.6f + sin(now * 0.005f) * 0.4f;
      if (f.y < 30) f.vy = 0.8f;
    }

    // (C) 位置更新
    f.x += f.vx;
    f.y += f.vy;

    // (D) 邊界碰撞反射 (避免游出水族箱外)
    if (f.x <= 8) {
      f.x = 8;
      f.dir = 1;
      f.vx = abs(f.vx);
    } else if (f.x >= 320 - f.w - 8) {
      f.x = 320 - f.w - 8;
      f.dir = -1;
      f.vx = -abs(f.vx);
    }

    if (f.y <= 16) {
      f.y = 16;
      f.vy = abs(f.vy);
    } else if (f.y >= SEABED_Y - f.h - 4) {
      f.y = SEABED_Y - f.h - 4;
      f.vy = -abs(f.vy);
    }

    // 擺尾動畫幀計時
    f.anim_tick++;
    if (f.anim_tick > 8) {
      f.frame = 1 - f.frame;
      f.anim_tick = 0;
      if (f.love_timer > 0) f.love_timer--;
      if (f.love_timer == 0 && f.is_puffed) f.is_puffed = false; // 河豚恢復正常
    }

    // (E) 繪製魚的新位置
    drawFish(f, is_night_mode);

    // 記錄位置供下一幀抹除
    f.old_x = (int)f.x;
    f.old_y = (int)f.y;
    f.old_w = f.w;
    f.old_h = f.h;
  }

  // 6. 動態海草搖曳 (左右兩側水草群)
  if (now - last_seaweed_update > 60) {
    seaweed_angle += 0.08f;
    // 左側海草群
    drawSeaweedBranch(18, SEABED_Y, 55, 0.0f);
    drawSeaweedBranch(32, SEABED_Y, 70, 0.8f);
    drawSeaweedBranch(46, SEABED_Y, 45, 1.6f);

    // 右側海草群
    drawSeaweedBranch(280, SEABED_Y, 65, 0.4f);
    drawSeaweedBranch(296, SEABED_Y, 80, 1.2f);
    drawSeaweedBranch(310, SEABED_Y, 50, 2.0f);
    last_seaweed_update = now;
  }

  delay(25); // 維持約 40 FPS 流暢度
}
