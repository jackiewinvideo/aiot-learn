@echo off
chcp 65001 >nul
echo ========================================
echo  ESP32-S3 Serial Monitor (COM10)
echo  按 Ctrl+C 可離開
echo ========================================
"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" device monitor -p COM10 -b 115200
pause
