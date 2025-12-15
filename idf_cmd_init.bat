@echo off
setlocal enabledelayedexpansion

set "DEFAULT_IDF_PATH=C:\\Espressif\\frameworks\\esp-idf-6.1-dev"
set "CONFIG_JSON=%~dp0esp_idf.json"

if not "%IDF_PATH%"=="" goto :have_idf

if exist "%CONFIG_JSON%" (
  for /f "usebackq tokens=*" %%I in (`powershell -NoProfile -Command "try { $json = Get-Content -Raw -Path '%CONFIG_JSON%' | ConvertFrom-Json; $path = $null; if ($json.idfSelectedId -and $json.idfInstalled) { $match = $json.idfInstalled | Where-Object { $_.idfId -eq $json.idfSelectedId }; if ($match) { $path = $match.path } }; if ($path) { Write-Output $path } } catch { }"`) do (
    set "IDF_PATH=%%I"
  )
)

:have_idf
if "%IDF_PATH%"=="" set "IDF_PATH=%DEFAULT_IDF_PATH%"

if not exist "%IDF_PATH%" (
  echo [idf_cmd_init] IDF_PATH "%IDF_PATH%" introuvable. Vérifiez l'installation.
  exit /b 1
)

echo [idf_cmd_init] IDF_PATH=%IDF_PATH%
call "%IDF_PATH%\export.bat"

where idf.py
idf.py --version

endlocal & set "IDF_PATH=%IDF_PATH%"
