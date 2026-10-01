@echo off
rem ============================================================
rem  HT5023 One-Click Flasher  --  double-click to launch
rem  Location-independent: finds the GUI script next to this
rem  .bat or in a tools\ subfolder, and discovers the project
rem  root from whatever folder you open in the app.
rem ============================================================
setlocal

set "APP="
if exist "%~dp0ht5023_flasher.py" set "APP=%~dp0ht5023_flasher.py"
if not defined APP if exist "%~dp0tools\ht5023_flasher.py" set "APP=%~dp0tools\ht5023_flasher.py"
if not defined APP (
    echo Could not find ht5023_flasher.py next to this launcher.
    pause
    exit /b 1
)

rem Prefer pythonw (no console window); fall back to python, then PATH.
set "PYW=%LocalAppData%\Programs\Python\Python311\pythonw.exe"
if not exist "%PYW%" set "PYW=%LocalAppData%\Programs\Python\Python312\pythonw.exe"
if not exist "%PYW%" set "PYW=pythonw"

start "" "%PYW%" "%APP%"
endlocal
