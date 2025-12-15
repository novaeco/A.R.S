param(
  [string]$ConfigPath = "$PSScriptRoot/esp_idf.json"
)

$defaultIdfPath = "C:\\Espressif\\frameworks\\esp-idf-6.1-dev"
$selectedIdfPath = $env:IDF_PATH

if (-not $selectedIdfPath -and (Test-Path $ConfigPath)) {
  try {
    $config = Get-Content -Raw -Path $ConfigPath | ConvertFrom-Json
    if ($config.idfSelectedId -and $config.idfInstalled) {
      $entry = $config.idfInstalled | Where-Object { $_.idfId -eq $config.idfSelectedId }
      if ($entry -and $entry.path) {
        $selectedIdfPath = $entry.path
      }
    }
  } catch {
    Write-Warning "[Initialize-Idf] Failed to parse $ConfigPath: $_"
  }
}

if (-not $selectedIdfPath) {
  $selectedIdfPath = $defaultIdfPath
}

if (-not (Test-Path $selectedIdfPath)) {
  throw "[Initialize-Idf] IDF_PATH '$selectedIdfPath' introuvable. Vérifiez l'installation."
}

$env:IDF_PATH = $selectedIdfPath
$exportScript = Join-Path $selectedIdfPath "export.ps1"
if (-not (Test-Path $exportScript)) {
  throw "[Initialize-Idf] export.ps1 introuvable dans $selectedIdfPath"
}

Write-Host "[Initialize-Idf] IDF_PATH=$env:IDF_PATH"
. $exportScript

$resolvedIdfPy = Get-Command idf.py -ErrorAction Stop
Write-Host "[Initialize-Idf] idf.py -> $($resolvedIdfPy.Source)"
idf.py --version
