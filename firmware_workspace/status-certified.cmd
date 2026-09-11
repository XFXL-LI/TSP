@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Get-FirmwareStatus.ps1" -Variant certified
