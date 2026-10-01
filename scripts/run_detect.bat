@echo off
set SPEED=%1
if "%SPEED%"=="" set SPEED=1000
if not exist "JLink.exe" (
  rem Assume JLink.exe is in PATH
  JLink.exe -device Cortex-M0 -if SWD -speed %SPEED% -autoconnect 1 -CommanderScript detect.jlink
) else (
  "JLink.exe" -device Cortex-M0 -if SWD -speed %SPEED% -autoconnect 1 -CommanderScript detect.jlink
)
