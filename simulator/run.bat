@echo off
chcp 65001 >nul
title Simulador ESP32 - Sistema de Porta Acessivel

cd /d "%~dp0"

echo [1/3] Verificando biblioteca MQTT (paho-mqtt)...
python -c "import paho.mqtt" 2>nul
if errorlevel 1 (
    echo Instalando paho-mqtt...
    pip install -r requirements.txt
)

echo [2/3] Iniciando servidor web local (porta 8080) e abrindo a Dashboard...
start "Servidor Web - Porta Acessivel" /min python -m http.server 8080 --directory "..\dashboard-web"
timeout /t 1 /nobreak >nul

start "" "http://localhost:8080/dashboard.html"

echo [3/3] Iniciando Simulador do ESP32 no terminal...
echo.
python simulador_esp32.py
pause
