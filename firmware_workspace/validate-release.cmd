@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Flash-Firmware.ps1" -ValidateOnly
