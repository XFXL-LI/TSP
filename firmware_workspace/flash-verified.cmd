@echo off
if "%~1"=="" (
  echo Usage: flash-verified.cmd COM9 FLASH
  exit /b 2
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Flash-Firmware.ps1" -Port "%~1" -ConfirmFlash "%~2"
