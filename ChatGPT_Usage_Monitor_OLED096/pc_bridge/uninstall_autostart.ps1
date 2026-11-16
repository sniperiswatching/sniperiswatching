$ErrorActionPreference = "Stop"

$startup = [Environment]::GetFolderPath("Startup")
$link = Join-Path $startup "ChatGPT_Usage_Monitor_OLED096_Bridge.lnk"

if (Test-Path -LiteralPath $link) {
    Remove-Item -LiteralPath $link -Force
    Write-Host "已删除自动启动项：$link"
} else {
    Write-Host "没有找到自动启动项。"
}
