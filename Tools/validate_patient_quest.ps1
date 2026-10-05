param(
    [string]$Serial = '',
    [string]$AdbPath = 'C:/Users/rja59/AppData/Local/Android/Sdk/platform-tools/adb.exe',
    [switch]$SkipInstall,
    [switch]$Rendered
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$apk = Join-Path $projectRoot 'Builds/Quest/NaturalPatientCare/Android_ASTC/CNABedToWheelchair-arm64.apk'
$reportDir = Join-Path $projectRoot 'Saved/PatientCare'
$logPath = Join-Path $reportDir 'QuestRuntimeProbe.log'
$errorPath = Join-Path $reportDir 'QuestLogcatCaptureErrors.log'
$package = 'com.YourCompany.CNABedToWheelchair'
$activity = "$package/com.epicgames.unreal.GameActivity"

if (!(Test-Path -LiteralPath $AdbPath)) { throw 'ADB is not installed at AdbPath.' }
if (!(Test-Path -LiteralPath $apk)) { throw "Verified APK not found: $apk" }
if (!$Serial) {
    $devices = @(& $AdbPath devices | Where-Object { $_ -match '^([^\s]+)\s+device$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if ($devices.Count -ne 1) { throw 'Connect one authorized Quest, or specify -Serial.' }
    $Serial = $devices[0]
}
if ($Serial -notmatch '^[a-zA-Z0-9._:-]+$') { throw 'Invalid ADB serial.' }
$deviceModel = (& $AdbPath -s $Serial shell getprop ro.product.model).Trim()
if ($LASTEXITCODE -ne 0) { throw 'The device is unavailable.' }
Write-Output "Patient validation on $deviceModel ($Serial)"

if (!$SkipInstall) {
    & $AdbPath -s $Serial install -r $apk
    if ($LASTEXITCODE -ne 0) { throw 'APK installation failed.' }
}
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
& $AdbPath -s $Serial shell am force-stop $package
$capture = Start-Process -FilePath $AdbPath -ArgumentList @('-s', $Serial, 'logcat', '-v', 'threadtime', '-T', '1', 'UE:V', 'UE4:V', '*:S') -WindowStyle Hidden -PassThru -RedirectStandardOutput $logPath -RedirectStandardError $errorPath
$passed = $false
try {
    $renderer = if ($Rendered) { '' } else { '-NullRHI' }
    # Synthetic hands deliberately bypass headset/controller tracking. -Rendered
    # enables Vulkan drawing; both modes exercise the packaged Android physics.
    $cmdline = " /Game/Project/Maps/CNA_Map_01 -nohmd $renderer -benchmark -fps=72 -ExecCmds=cna.Patient.SmokeBed20"
    & $AdbPath -s $Serial shell "am start -n $activity --es cmdline '$cmdline'"
    if ($LASTEXITCODE -ne 0) { throw 'Unable to launch the packaged runtime probe.' }
    $deadline = (Get-Date).AddMinutes(25)
    $lastAttempt = ''
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 5
        $tail = @(Get-Content -LiteralPath $logPath -Tail 300 -ErrorAction SilentlyContinue)
        $progress = $tail | Where-Object { $_ -match 'PatientProbe: attempt.*PASS' } | Select-Object -Last 1
        if ($progress -and $progress -ne $lastAttempt) { Write-Output $progress; $lastAttempt = $progress }
        if ($tail -match 'PatientProbe: FAIL') { throw "Packaged patient probe failed. See $logPath" }
        if ($tail -match 'PatientProbe: PASS completed=20/20') { $passed = $true; break }
        if ($capture.HasExited) { throw 'ADB log capture ended before validation completed.' }
    }
    if (!$passed) { throw "No completion result within 25 minutes. See $logPath" }
    Write-Output 'PASS: 20 packaged bed attempts and final belt-to-chair transfer.'
    Write-Output 'Physical controller feel and video/answer interaction still require headset acceptance.'
}
finally {
    if (!$capture.HasExited) { Stop-Process -Id $capture.Id -Force }
    if ($passed) {
        & $AdbPath -s $Serial shell am force-stop $package
        & $AdbPath -s $Serial shell am start -n $activity
    }
}
