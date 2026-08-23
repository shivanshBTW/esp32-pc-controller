$env:IDF_PATH = "C:\esp\v5.3.2\esp-idf"

if (-not (Test-Path "$env:IDF_PATH\export.ps1")) {
    throw "ESP-IDF v5.3.2 export.ps1 not found at $env:IDF_PATH."
}

& "$env:IDF_PATH\export.ps1"

$env:PATH = (($env:PATH -split ';') | Where-Object {
    $_ -and ($_ -notlike "*\.espressif\tools\idf-exe\*")
}) -join ';'

$env:PATH = "$env:IDF_PATH\tools;$env:PATH"

function global:idf.py {
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" @args
}