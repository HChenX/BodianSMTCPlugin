Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    return $netTask.Result
}

[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.MediaControl, ContentType = WindowsRuntime] | Out-Null
$asyncOp = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()
$manager = Await $asyncOp ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])

$sessions = $manager.GetSessions()
foreach ($s in $sessions) {
    $info = $s.GetPlaybackInfo()
    $timeline = $s.GetTimelineProperties()
    $media = Await ($s.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
    Write-Host "SourceAppId: $($s.SourceAppUserModelId)"
    Write-Host "Title:       $($media.Title)"
    Write-Host "Artist:      $($media.Artist)"
    Write-Host "Status:      $($info.PlaybackStatus)"
    Write-Host "Position:    $($timeline.Position)"
    Write-Host "EndTime:     $($timeline.EndTime)"
    Write-Host "LastUpdated: $($timeline.LastUpdatedTime)"
    Write-Host "----------------------------------------"
}
