$events = Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000} -MaxEvents 5 -ErrorAction SilentlyContinue
foreach ($e in $events) {
    if ($e.TimeCreated -gt (Get-Date).AddMinutes(-10)) {
        Write-Host "Time: $($e.TimeCreated)"
        Write-Host "Message: $($e.Message)"
        Write-Host "---"
    }
}
