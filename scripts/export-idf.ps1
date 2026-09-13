$ErrorActionPreference = 'Stop'
$idfPath = 'D:\DevTool\Espressif\frameworks\esp-idf-v5.5.4'
$pythonExe = 'D:\DevTool\Espressif\tools\idf-python\3.11.2\python.exe'
$activateScript = "$idfPath\tools\activate.py"

foreach ($requiredPath in @($pythonExe, $activateScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required ESP-IDF file not found: $requiredPath"
    }
}

$idfExports = & $pythonExe $activateScript --export
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

. $idfExports
& idf.py @args
exit $LASTEXITCODE
