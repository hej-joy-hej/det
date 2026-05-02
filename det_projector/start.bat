@echo off
setlocal

rem Navigate to the directory containing this script
cd /d "%~dp0"

rem Kill any existing servers on these ports
echo Cleaning up any existing servers...
for /f "tokens=5" %%a in ('netstat -ano 2^>nul ^| findstr "LISTENING" ^| findstr ":8000"') do taskkill /F /PID %%a >nul 2>&1
for /f "tokens=5" %%a in ('netstat -ano 2^>nul ^| findstr "LISTENING" ^| findstr ":8765"') do taskkill /F /PID %%a >nul 2>&1

rem Start HTTP server in a minimized window
echo Starting HTTP server on port 8000...
start "HTTP Server" /min cmd /c python -m http.server 8000

timeout /t 1 /nobreak >nul

rem Start WebSocket server in a visible window
echo Starting WebSocket server on port 8765...
start "WebSocket Server" cmd /k python video_controller.py

timeout /t 2 /nobreak >nul

rem Open display in default browser
echo Opening display in browser...
start "" "http://localhost:8000/display.html"

echo.
echo ====================================
echo Installation is running!
echo.
echo Fullscreen: F11
echo.
echo To stop: close the "HTTP Server" and
echo          "WebSocket Server" taskbar windows,
echo          or close this window.
echo ====================================
pause
