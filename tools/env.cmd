@echo off
set "IDF_PATH=C:\\Espressif\\frameworks\\esp-idf-6.1-dev"
set "EXPORT_BAT=%IDF_PATH%\export.bat"
if not exist "%EXPORT_BAT%" (
  echo [tools/env.cmd] export.bat introuvable dans %IDF_PATH%
  exit /b 1
)
call "%EXPORT_BAT%"
echo [tools/env.cmd] IDF_PATH=%IDF_PATH%
where idf.py
idf.py --version
