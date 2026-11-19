// ============================================================
// ESP32-S3 DHT11 溫濕度 Web Dashboard
// 感測器：DHT-11 接 GPIO 15
// 功能：連上 Wi-Fi，開啟 Web Server，提供即時溫濕度 Dashboard
// 瀏覽器開啟 http://<ESP32的IP> 即可查看
// ============================================================

#include <Arduino.h>       // Arduino 核心標頭
#include <WiFi.h>          // ESP32 Wi-Fi 庫
#include <WebServer.h>     // ESP32 內建 HTTP Web Server 庫
#include <DHT.h>           // Adafruit DHT 感測器庫

// ── Wi-Fi 設定 ─────────────────────────────────────────────
const char* SSID     = "mcugust";   // Wi-Fi 名稱（SSID）
const char* PASSWORD = "mcu18226";  // Wi-Fi 密碼

// ── AP 備援模式設定（Wi-Fi 連不上時，ESP32 自己開熱點）─────
const char* AP_SSID  = "ESP32-DHT11";  // ESP32 熱點名稱
const char* AP_PASS  = "12345678";     // ESP32 熱點密碼（至少8碼）

// ── DHT11 感測器設定 ────────────────────────────────────────
#define DHT_PIN  15        // DHT11 資料腳位：GPIO 15
#define DHT_TYPE DHT11     // 感測器型號：DHT11

DHT dht(DHT_PIN, DHT_TYPE); // 建立 DHT 物件

// ── Web Server（監聽 80 port）──────────────────────────────
WebServer server(80);      // 建立 HTTP 伺服器，Port 80（標準 HTTP）

// ── 感測器數值儲存 ──────────────────────────────────────────
float temperature = 0.0;   // 溫度（°C）
float humidity    = 0.0;   // 濕度（%）
unsigned long lastRead = 0; // 上次讀取感測器的時間戳

// ── Dashboard HTML 頁面 ────────────────────────────────────
// 使用 R"rawstring(...)rawstring" 原始字串，避免引號跳脫問題
// JavaScript 每 3 秒向 /data 取得最新 JSON，更新畫面數值
const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 環境監測</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, rgb(15, 52, 96) 100%);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      color: #fff;
    }
    h1 {
      font-size: 1.6rem;
      letter-spacing: 2px;
      margin-bottom: 8px;
      color: #e0e0e0;
    }
    .subtitle {
      font-size: 0.85rem;
      color: #7f8c8d;
      margin-bottom: 40px;
      letter-spacing: 1px;
    }
    .cards {
      display: flex;
      gap: 30px;
      flex-wrap: wrap;
      justify-content: center;
    }
    .card {
      background: rgba(255,255,255,0.07);
      border: 1px solid rgba(255,255,255,0.12);
      border-radius: 20px;
      padding: 40px 50px;
      text-align: center;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 32px rgba(0,0,0,0.3);
      transition: transform 0.2s;
      min-width: 200px;
    }
    .card:hover { transform: translateY(-4px); }
    .card-icon { font-size: 3rem; margin-bottom: 12px; }
    .card-label {
      font-size: 0.9rem;
      color: #95a5a6;
      letter-spacing: 2px;
      text-transform: uppercase;
      margin-bottom: 10px;
    }
    .card-value {
      font-size: 3.5rem;
      font-weight: 700;
      line-height: 1;
    }
    .card-unit {
      font-size: 1.4rem;
      font-weight: 300;
      color: #bdc3c7;
    }
    .temp .card-value { color: #ff6b6b; }
    .humi .card-value { color: #48dbfb; }
    .status-bar {
      margin-top: 40px;
      font-size: 0.8rem;
      color: #636e72;
      letter-spacing: 1px;
    }
    .dot {
      display: inline-block;
      width: 8px; height: 8px;
      border-radius: 50%;
      background: #00b894;
      margin-right: 6px;
      animation: pulse 1.5s infinite;
    }
    @keyframes pulse {
      0%,100% { opacity: 1; }
      50%      { opacity: 0.3; }
    }
  </style>
</head>
<body>
  <h1>🌡️ 環境監測 Dashboard</h1>
  <div class="subtitle">ESP32-S3 ・ DHT-11 ・ GPIO 15</div>

  <div class="cards">
    <div class="card temp">
      <div class="card-icon">🌡️</div>
      <div class="card-label">溫度</div>
      <div class="card-value"><span id="temp">--</span><span class="card-unit"> °C</span></div>
    </div>
    <div class="card humi">
      <div class="card-icon">💧</div>
      <div class="card-label">濕度</div>
      <div class="card-value"><span id="humi">--</span><span class="card-unit"> %</span></div>
    </div>
  </div>

  <div class="status-bar">
    <span class="dot"></span>
    每 3 秒自動更新 ・ 上次更新：<span id="ts">--</span>
  </div>

  <style>
    .chart-wrap {
      margin-top: 36px;
      background: rgba(255,255,255,0.05);
      border: 1px solid rgba(255,255,255,0.1);
      border-radius: 16px;
      padding: 20px 20px 12px;
      width: 90%;
      max-width: 680px;
    }
    .chart-title { font-size:0.9rem; color:#b2bec3; letter-spacing:1px; margin-bottom:8px; }
    .chart-legend { font-size:0.78rem; margin-bottom:10px; }
    .leg-t { color:#ff6b6b; margin-right:16px; }
    .leg-h { color:#48dbfb; }
    #chart { width:100%; display:block; }
  </style>

  <div class="chart-wrap">
    <div class="chart-title">📈 歷史趨勢（最近 30 筆，每 3 秒更新）</div>
    <div class="chart-legend">
      <span class="leg-t">▬ 溫度 (°C)｜左軸 0–50</span>
      <span class="leg-h">▬ 濕度 (%)｜右軸 0–100</span>
    </div>
    <canvas id="chart" width="640" height="180"></canvas>
  </div>

  <script>
    const MAX_PTS = 30;
    let tArr=[], hArr=[], lArr=[];

    function drawChart() {
      const cv = document.getElementById('chart');
      const ctx = cv.getContext('2d');
      const W=cv.width, H=cv.height;
      const P={t:18,r:48,b:28,l:42};
      const cW=W-P.l-P.r, cH=H-P.t-P.b;

      ctx.clearRect(0,0,W,H);

      // 圖表背景
      ctx.fillStyle='rgba(255,255,255,0.03)';
      ctx.fillRect(P.l,P.t,cW,cH);

      // 格線 + 左右 Y 軸刻度
      for(let i=0;i<=5;i++){
        const y=P.t+(cH/5)*i;
        ctx.strokeStyle='rgba(255,255,255,0.08)';
        ctx.lineWidth=1;
        ctx.beginPath(); ctx.moveTo(P.l,y); ctx.lineTo(P.l+cW,y); ctx.stroke();
        ctx.font='10px Arial';
        ctx.fillStyle='#ff9f9f'; ctx.textAlign='right';
        ctx.fillText((50-i*10)+'°', P.l-4, y+4);       // 溫度軸（左）
        ctx.fillStyle='#7fe8ff'; ctx.textAlign='left';
        ctx.fillText((100-i*20)+'%', P.l+cW+4, y+4);   // 濕度軸（右）
      }

      if(tArr.length < 2) return;
      const n = tArr.length;

      // 畫折線函式
      function line(arr, maxV, color){
        ctx.strokeStyle=color; ctx.lineWidth=2;
        ctx.shadowColor=color; ctx.shadowBlur=6;
        ctx.beginPath();
        arr.forEach((v,i)=>{
          const x = P.l + (i/(MAX_PTS-1))*cW;
          const y = P.t + cH - (Math.min(Math.max(v,0),maxV)/maxV)*cH;
          i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
        });
        ctx.stroke();
        ctx.shadowBlur=0;
        // 最新值的圓點與數值標籤
        const li=arr.length-1;
        const lx=P.l+(li/(MAX_PTS-1))*cW;
        const ly=P.t+cH-(Math.min(Math.max(arr[li],0),maxV)/maxV)*cH;
        ctx.fillStyle=color;
        ctx.beginPath(); ctx.arc(lx,ly,4,0,Math.PI*2); ctx.fill();
        ctx.font='bold 11px Arial'; ctx.textAlign='center';
        ctx.fillText(arr[li].toFixed(1), lx, ly-10);
      }

      line(tArr, 50,  '#ff6b6b');   // 溫度（最大 50°C）
      line(hArr, 100, '#48dbfb');   // 濕度（最大 100%）

      // X 軸時間標籤（首、中、末）
      ctx.fillStyle='#636e72'; ctx.font='10px Arial'; ctx.textAlign='center';
      ctx.fillText(lArr[0],               P.l,                    H-4);
      ctx.fillText(lArr[Math.floor(n/2)], P.l+(Math.floor(n/2)/(MAX_PTS-1))*cW, H-4);
      ctx.fillText(lArr[n-1],             P.l+((n-1)/(MAX_PTS-1))*cW,           H-4);
    }

    function update() {
      fetch('/data')
        .then(r => r.json())
        .then(d => {
          const t=parseFloat(d.temperature), h=parseFloat(d.humidity);
          document.getElementById('temp').textContent = isNaN(t)?'--':t.toFixed(1);
          document.getElementById('humi').textContent = isNaN(h)?'--':h.toFixed(1);
          const ts = new Date().toLocaleTimeString();
          document.getElementById('ts').textContent = ts;
          if(!isNaN(t) && !isNaN(h)){
            if(tArr.length>=MAX_PTS){ tArr.shift(); hArr.shift(); lArr.shift(); }
            tArr.push(t); hArr.push(h); lArr.push(ts);
            drawChart();
          }
        })
        .catch(()=>{
          document.getElementById('temp').textContent='--';
          document.getElementById('humi').textContent='--';
        });
    }
    update();
    setInterval(update, 3000);
  </script>
</body>
</html>
)rawhtml";

// ── 路由：/ → 回傳 Dashboard HTML ──────────────────────────
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE); // 回傳 HTTP 200，Content-Type: text/html
}

// ── 路由：/data → 回傳 JSON 格式的溫濕度數值 ──────────────
void handleData() {
  // 組合 JSON 字串（格式：{"temperature":25.5,"humidity":60.2}）
  String json = "{";
  json += "\"temperature\":" + String(temperature, 1) + ","; // 溫度，保留 1 位小數
  json += "\"humidity\":"    + String(humidity,    1);       // 濕度，保留 1 位小數
  json += "}";
  server.send(200, "application/json", json); // 回傳 HTTP 200，Content-Type: JSON
}

// ── 路由：404 頁面 ──────────────────────────────────────────
void handleNotFound() {
  server.send(404, "text/plain", "404 Not Found"); // 找不到路由時回傳 404
}

// ============================================================
// setup() — 開機初始化
// ============================================================
void setup() {
  Serial.begin(115200);              // 啟動序列埠（鮑率 115200）
  Serial.println("\n=== ESP32-S3 DHT11 Dashboard 啟動中 ===");

  // ── 初始化 DHT11 感測器 ──────────────────────────────────
  dht.begin();                       // 啟動 DHT 感測器驅動
  Serial.println("[DHT] DHT11 初始化完成，接腳：GPIO 15");

  // ── 連接 Wi-Fi ───────────────────────────────────────────
  Serial.printf("[WiFi] 嘗試連接：%s\n", SSID);
  Serial.println("[WiFi] 注意：ESP32 只支援 2.4GHz，不支援 5GHz！");
  WiFi.mode(WIFI_STA);               // 設定為 Station 模式（連接路由器）
  WiFi.begin(SSID, PASSWORD);        // 開始連接 Wi-Fi

  int retries = 0;                   // 重試計數
  while (WiFi.status() != WL_CONNECTED) { // 等待連線成功
    delay(500);                      // 每 500ms 檢查一次
    Serial.print(".");
    retries++;
    if (retries > 20) {              // 超過 10 秒仍未連線 → 切換 AP 模式
      Serial.println("\n[WiFi] 連線失敗！切換為 AP 熱點模式...");
      WiFi.mode(WIFI_AP);            // 切換為 AP（Access Point）模式
      WiFi.softAP(AP_SSID, AP_PASS); // 建立 ESP32 熱點
      Serial.println("=========================================");
      Serial.printf("[AP] 熱點名稱：%s\n", AP_SSID);
      Serial.printf("[AP] 熱點密碼：%s\n", AP_PASS);
      Serial.printf("[AP] 連上熱點後，開啟瀏覽器輸入：http://192.168.4.1\n");
      Serial.println("=========================================");
      break;                         // 跳出等待迴圈
    }
  }

  if (WiFi.status() == WL_CONNECTED) { // STA 模式連線成功
    Serial.println();
    Serial.println("=========================================");
    Serial.printf("[WiFi] 連線成功！\n");
    Serial.printf("[WiFi] 請用瀏覽器開啟：http://%s\n",
      WiFi.localIP().toString().c_str());
    Serial.println("=========================================");
  }

  // ── 設定 Web Server 路由 ─────────────────────────────────
  server.on("/",      handleRoot);    // 根路由 → Dashboard HTML
  server.on("/data",  handleData);    // /data  → JSON 溫濕度數值
  server.onNotFound(handleNotFound);  // 其他路由 → 404

  server.begin();                    // 啟動 Web Server（開始監聽 Port 80）
  Serial.println("[Server] Web Server 已啟動");
  Serial.println("=== 請用瀏覽器開啟上方 IP 位址 ===\n");
}

// ============================================================
// loop() — 主循環
// ============================================================
void loop() {
  server.handleClient();             // 處理 HTTP 請求（必須在 loop 中持續呼叫）

  // 每 2 秒讀取一次感測器（DHT11 最快取樣間隔約 1 秒）
  if (millis() - lastRead >= 2000) {
    lastRead = millis();             // 更新上次讀取時間

    float t = dht.readTemperature(); // 讀取溫度（°C）
    float h = dht.readHumidity();    // 讀取濕度（%）

    if (isnan(t) || isnan(h)) {      // 若讀取失敗（回傳 NaN）則跳過
      Serial.println("[DHT] 讀取失敗，請確認接線");
    } else {
      temperature = t;               // 更新全域溫度變數
      humidity    = h;               // 更新全域濕度變數
      Serial.printf("[DHT] 溫度：%.1f °C  濕度：%.1f %%\n", temperature, humidity);
    }
  }
}
