@echo off
setlocal
set "PYTHONNOUSERSITE=1"
set "PYTHONUTF8=1"
set "PYTHONPATH=%~dp0serial-monitor\vendor"
"%~dp0serial-monitor\python\python.exe" "%~dp0benchmark-ui-scenes.py" %*
