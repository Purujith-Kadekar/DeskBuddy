@echo off
rem ============================================================
rem  Desk Buddy wireless flash (no USB cable needed).
rem
rem    ota_update.bat                  -> flashes deskbuddy.local
rem    ota_update.bat 192.168.1.42     -> flashes that IP instead
rem                                        (use the IP if .local does not
rem                                         resolve on your PC)
rem
rem  What it does: builds the firmware with PlatformIO, then calls the
rem  same OTA uploader PIO itself uses (espota.py) - with the password,
rem  which PIO has no way to pass through platformio.ini. The password
rem  is read from include\wifi_credentials.h, so it is never committed.
rem ============================================================
cd /d "%~dp0.."

set "HOST=%~1"
if "%HOST%"=="" set "HOST=deskbuddy.local"

set "PASS="
for /f "tokens=3" %%a in ('findstr /b /c:"#define OTA_PASSWORD" include\wifi_credentials.h') do set "PASS=%%~a"
if not defined PASS (
    echo [ERROR] could not read OTA_PASSWORD from include\wifi_credentials.h
    exit /b 1
)

set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PY%" set "PY=python"

set "ESPOTA=%USERPROFILE%\.platformio\packages\framework-arduinoespressif32\tools\espota.py"
if not exist "%ESPOTA%" (
    for /d %%d in ("%USERPROFILE%\.platformio\packages\framework-arduinoespressif32@*") do set "ESPOTA=%%d\tools\espota.py"
)
if not exist "%ESPOTA%" (
    echo [ERROR] espota.py not found - flash over USB once so PlatformIO
    echo         downloads the ESP32 framework, then try again.
    exit /b 1
)

echo Building firmware ...
pio run
if errorlevel 1 exit /b 1

echo.
echo Flashing %HOST% over the air (the buddy freezes for about a minute) ...
"%PY%" "%ESPOTA%" --debug --progress -i %HOST% --auth=%PASS% -f .pio\build\esp32c3\firmware.bin
if errorlevel 1 (
    echo.
    echo [FAILED] If it says "no answer"/timeout: is the buddy plugged in and on
    echo           WiFi? Try the IP address instead of deskbuddy.local
    echo           ^(find it in Menu - Info on the buddy^).
    echo.
    echo   "Authenticating ... OK" then "No response from device" = the buddy
    echo   is fine but could not call BACK to this PC. Windows Firewall or a VPN
    echo   is blocking it. Fix: allow python.exe in the firewall ^(Private AND
    echo   Public^) or switch the VPN off. Or skip this script:
    echo.
    echo      open http://%HOST%:8080/update in any browser
    echo      ^(user deskbuddy, your OTA password^), pick
    echo      .pio\build\esp32c3\firmware.bin - upload - done.
    echo.
    echo   That door only needs PC-to-buddy traffic and always works. The
    echo   buddy being asleep is no obstacle; it wakes for the flash.
    exit /b 1
)
echo.
echo Done - the buddy reboots itself into the new firmware.
