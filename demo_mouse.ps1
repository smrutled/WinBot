$commands = @(
    '{"id":1, "tool":"input_human_move", "args":{"x": 200, "y": 200}}',
    '{"id":2, "tool":"input_human_move", "args":{"x": 800, "y": 200}}',
    '{"id":3, "tool":"input_human_move", "args":{"x": 800, "y": 800}}',
    '{"id":4, "tool":"input_human_move", "args":{"x": 200, "y": 800}}',
    '{"id":5, "tool":"input_human_move", "args":{"x": 200, "y": 200}}'
)

$commands | Out-File -FilePath "demo_input.txt" -Encoding ascii

Write-Host "Watch your mouse cursor! It should draw a human-like square on your screen."
Get-Content "demo_input.txt" | .\build\bin\Release\WinBot.exe --mcp

Remove-Item "demo_input.txt"
Write-Host "Demonstration complete."
