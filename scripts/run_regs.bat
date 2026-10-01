@echo off
set SPEED=%1
if "%SPEED%"=="" set SPEED=1000
if not exist "JLink.exe" (
  JLink.exe -device Cortex-M0 -if SWD -speed %SPEED% -autoconnect 1 -CommanderScript regs.jlink
) else (
  "JLink.exe" -device Cortex-M0 -if SWD -speed %SPEED% -autoconnect 1 -CommanderScript regs.jlink
)
