$ErrorActionPreference = "Stop"

$bridge = Join-Path $PSScriptRoot "bridge.py"
if (-not (Test-Path -LiteralPath $bridge)) {
    throw "bridge.py was not found: $bridge"
}

$startup = [Environment]::GetFolderPath("Startup")
if ([string]::IsNullOrWhiteSpace($startup)) {
    throw "The Windows Startup folder was not found"
}

$link = Join-Path $startup "ChatGPT_Usage_Monitor_OLED096_Bridge.lnk"
$python = Get-Command pythonw.exe -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command pyw.exe -ErrorAction SilentlyContinue
}
if (-not $python) {
    $python = Get-Command python.exe -ErrorAction Stop
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($link)
$shortcut.TargetPath = $python.Source
$shortcut.Arguments = '"' + $bridge + '"'
$shortcut.WorkingDirectory = $PSScriptRoot
$shortcut.WindowStyle = 7
$shortcut.Description = "ChatGPT Usage OLED096 bridge"
$shortcut.Save()

Write-Host "Autostart shortcut created: $link"
Write-Host "Bridge script: $bridge"
