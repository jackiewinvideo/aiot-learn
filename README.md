# aiot-learn

## CYD 溫濕度上傳 ThingSpeak

1. 將 `.env.example` 複製為 `.env`，填入 2.4 GHz 手機熱點的 `WIFI_SSID`、`WIFI_PASSWORD`，以及頻道的 `THINGSPEAK_WRITE_API_KEY`。現有 `.env` 可直接修改，不必重新複製。請勿提交 `.env` 或分享密鑰。
2. 在 ThingSpeak 頻道設定中啟用 Field 1（溫度）與 Field 2（濕度）。Write API Key 決定資料上傳的頻道；程式不使用 Channel ID 或 Read API Key。
3. 使用 PlatformIO 的 `cyd` 環境編譯及燒錄，開啟手機熱點，並以 115200 baud 查看序列埠訊息。程式每 20 秒嘗試上傳；感測器、Wi-Fi、時間同步或 HTTPS 出錯時會在序列埠顯示原因。

`.env` 會在編譯時轉成 `.pio` 下的設定檔，Wi-Fi 密碼及 Write API Key 也會包含在燒錄的韌體內。變更設定後需重新編譯並燒錄。

## 本機 ThingSpeak 儀表板

網頁儀表板會使用 `.env` 中的 `THINGSPEAK_CHANNEL_ID` 與 `THINGSPEAK_READ_API_KEY`，並透過本機 Python 伺服器讀取 ThingSpeak；API Key 不會傳給瀏覽器。請在專案根目錄執行：

```powershell
python dashboard\server.py
```

保持伺服器的終端機視窗開啟，並在同一台電腦的瀏覽器開啟 `http://127.0.0.1:8000/`。不要直接開啟 HTML 檔案或使用 Live Server，否則無法存取資料接口。需先在 `.env` 設定頻道 ID 與 Read API Key；若頻道為公開頻道，Read API Key 可留空；修改 `.env` 後需重新啟動伺服器。

### 手機存取

1. 手機與電腦連接同一個 Wi-Fi。在電腦的 PowerShell 執行 `ipconfig`，找到 Wi-Fi 的 IPv4 位址。
2. 如果儀表板已在執行，先在原終端機按 `Ctrl+C` 停止，再用該 IPv4 位址啟動；例如電腦位址為 `192.168.1.10`：

   ```powershell
   python dashboard\server.py --host 192.168.1.10
   ```

3. 保持終端機開啟，在手機瀏覽器輸入 `http://192.168.1.10:8000/`，將範例位址換成電腦實際的 Wi-Fi IPv4 位址。電腦也可用相同網址開啟。

若無法連線，確認 Windows 防火牆允許 Python 在私人網路上使用 TCP 8000 埠，並確認 Wi-Fi 沒有啟用裝置隔離。這個伺服器沒有登入驗證，能連到該位址的裝置都可查看儀表板及感測資料；請只在信任的網路使用，不要對外網開放。

### 使用 Tailscale 從外網私人存取

1. 在電腦與手機安裝並登入 Tailscale，讓手機成為同一個 Tailnet 中獲准連線的裝置。保持電腦開機，並讓兩個裝置的 Tailscale 保持連線。
2. 在電腦的 PowerShell 執行 `tailscale ip -4`，取得電腦的 Tailscale IPv4 位址。如果儀表板已在執行，先在原終端機按 `Ctrl+C` 停止，再用該位址啟動；例如：

   ```powershell
   python dashboard\server.py --host 100.100.100.100
   ```

3. 保持終端機開啟。在手機開啟 Tailscale 後，於瀏覽器輸入 `http://100.100.100.100:8000/`，將範例位址換成電腦實際的 Tailscale IPv4 位址。手機不需要與電腦使用同一個 Wi-Fi。

此方式不會建立公開網址或要求路由器轉發埠。儀表板本身沒有登入驗證；可連到電腦該埠的 Tailnet 裝置都能查看資料，請依需要限制 Tailscale 存取規則。若無法連線，請確認雙方已連上 Tailscale、存取規則允許連線，且 Windows 防火牆允許該網路介面的 TCP 8000 埠。

## 本機 PostgreSQL 資料庫

使用 Docker Desktop 啟動後，在專案根目錄執行：

```powershell
docker compose -p aiot-iot-db -f compose.iot.yaml up -d
```

首次使用前，請在專案根目錄建立 `.iot-postgres.env`，內容為 `POSTGRES_PASSWORD=你的密碼`；使用強密碼，不要提交或分享此檔案。Docker 容器設定也會包含此密碼，請限制 Docker 的存取權。密碼只在資料卷首次初始化時設定；修改檔案不會更改已存在的資料庫密碼。

在 DBeaver 新增 PostgreSQL 連線，主機為 `127.0.0.1`、連接埠為 `5432`、資料庫為 `iot_dashboard`、使用者為 `postgres`，密碼填入上述檔案中的值。連線只開放給這台電腦。

首次建立資料卷時，Docker 會執行 [create_iot_schema.sql](./create_iot_schema.sql)，建立資料表和檢視表。停止容器使用 `docker compose -p aiot-iot-db -f compose.iot.yaml stop`；資料會保留在 Docker volume。不要使用帶 `-v` 的 `down` 指令，否則會刪除資料。修改 schema 腳本不會自動更新現有資料庫，需另行執行資料庫遷移。

目前儀表板仍從 ThingSpeak 讀取資料，尚未連接此資料庫，也不會自動寫入感測數據。