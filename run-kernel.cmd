@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0kernel\build.ps1" run
if errorlevel 1 pause
