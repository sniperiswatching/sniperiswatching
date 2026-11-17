$ErrorActionPreference = "Stop"

$startup = [Environment]::GetFolderPath("Startup")
$link = Join-Path $startup "ChatGPT_Usage_Monitor_OLED096_Bridge.lnk"

if (Test-Path -LiteralPath $link) {
    Remove-Item -LiteralPath $link -Force
    Write-Host "Autostart shortcut removed: $link"
} else {
    Write-Host "Autostart shortcut was not found."
}
