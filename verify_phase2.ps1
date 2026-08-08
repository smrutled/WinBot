$testJson = @'
{"id":1, "tool":"lua_exec", "args":{"code":"winbot.log('Starting verification...'); winbot.sleep(100); winbot.click(500, 500); return 'Verification passed!';"}}
'@

Write-Host "Running WinBot Phase 2 Verification..."
$exePath = ".\build\bin\Release\WinBot.exe"
if (-not (Test-Path $exePath)) {
    $exePath = ".\build\bin\WinBot.exe"
}
$output = $testJson | & $exePath --mcp
Write-Host "Output from WinBot:"
Write-Host $output

if ($output -like "*Verification passed!*") {
    Write-Host "PHASE 2 VERIFIED SUCCESSFULLY."
} else {
    Write-Host "PHASE 2 VERIFICATION FAILED."
}
