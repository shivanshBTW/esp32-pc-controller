$IdfPath = @(
    "C:\esp\v5.3.2\esp-idf",
    "C:\esp\5.3.2\esp-idf",
    "C:\esp\release-v5.3.2\esp-idf"
) | Where-Object { Test-Path "$_\export.ps1" } | Select-Object -First 1

if (-not $IdfPath) {
    throw "ESP-IDF v5.3.2 export.ps1 not found under C:\esp. Run 'eim list' and add the listed path to .repo-env.ps1."
}

$env:IDF_PATH = $IdfPath
& "$env:IDF_PATH\export.ps1"

$env:PATH = (($env:PATH -split ';') | Where-Object {
    $_ -and ($_ -notlike "*\.espressif\tools\idf-exe\*")
}) -join ';'

$env:PATH = "$env:IDF_PATH\tools;$env:PATH"

function global:idf.py {
    $python = if ($env:IDF_PYTHON_ENV_PATH -and (Test-Path "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe")) {
        "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe"
    } else {
        "python"
    }

    & $python "$env:IDF_PATH\tools\idf.py" @args
}