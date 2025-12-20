$env:IDF_PATH = "C:\\Espressif\\frameworks\\esp-idf-6.1-dev"
$export = Join-Path $env:IDF_PATH "export.ps1"
if (-not (Test-Path $export)) {
  throw "[tools/env.ps1] export.ps1 introuvable dans $env:IDF_PATH"
}
. $export
Write-Host "[tools/env.ps1] IDF_PATH=$env:IDF_PATH"
$resolved = Get-Command idf.py -ErrorAction Stop
Write-Host "[tools/env.ps1] idf.py -> $($resolved.Source)"
idf.py --version
