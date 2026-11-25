param(
    [string]$PackageRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
    [string]$RuntimeDirectory = '',
    [string]$ProjectDirectory = '',
    [string]$OutputDirectory = '',
    [switch]$SilentAudio,
    [ValidateRange(10,600)][int]$TimeoutSeconds = 60
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
    throw 'Multi-instance verification requires 64-bit Windows PowerShell.'
}
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path $package "Engine/Runtime" }
$sourceRuntime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
if (-not $ProjectDirectory) { $ProjectDirectory = Join-Path $package 'Projects/Grass1000' }
$sourceProject = (Resolve-Path -LiteralPath $ProjectDirectory).Path
if (-not (Get-ChildItem -LiteralPath $sourceProject -Filter '*.rice' -File)) {
    throw 'ProjectDirectory must contain a saved .rice file and its complete assets.'
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $package ('Engine/Cache/multi-instance/run-' + [guid]::NewGuid().ToString('N')) }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Choose a new output directory; verification never deletes existing files.' }
$runtime = Join-Path $output 'Runtime with spaces'
$working = Join-Path $output 'Different working directory'
$project = Join-Path $output 'Projects/Shared Grass Demo'
$executable = Join-Path $runtime 'RicePlayer.exe'
$children = [Collections.Generic.List[object]]::new()
$results = [Collections.Generic.List[object]]::new()
$passed = $false
$failure = ''
$projectUnchanged = $false
$binaryHash = ''
foreach ($directory in @($runtime,$working,$project)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

function Project-Fingerprint([string]$Directory) {
    return ((Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Directory.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join "`n")
}

function Start-Player([string]$Name,[bool]$Smoke) {
    $stdout = Join-Path $output ($Name + '.stdout.txt')
    $stderr = Join-Path $output ($Name + '.stderr.txt')
    $arguments = @('--project',('"{0}"' -f $project))
    if ($Smoke) { $arguments += '--smoke' }
    if ($SilentAudio) { $arguments += '--silent-audio' }
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $working `
        -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    # Retain the handle before exit so Windows PowerShell can still read ExitCode.
    $null = $process.Handle
    $child = [pscustomobject]@{ name=$Name; process=$process; stdout=$stdout; stderr=$stderr }
    $children.Add($child)
    return $child
}

function Assert-Alive([object[]]$Group) {
    foreach ($child in $Group) {
        $child.process.Refresh()
        if ($child.process.HasExited) {
            throw "Player exited unexpectedly: $($child.name), PID $($child.process.Id). See $($child.stderr)."
        }
    }
}

function Wait-ForWindows([object[]]$Group) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        Assert-Alive $Group
        $ready = @($Group | Where-Object { $_.process.MainWindowHandle -ne [IntPtr]::Zero }).Count -eq $Group.Count
        if ($ready) { return }
        Start-Sleep -Milliseconds 50
    } while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw 'Timed out waiting for all player windows.'
}

function Measure-Progress([object[]]$Group) {
    Assert-Alive $Group
    $before = @{}
    foreach ($child in $Group) { $before[$child.process.Id] = $child.process.TotalProcessorTime.TotalMilliseconds }
    Start-Sleep -Milliseconds 1000
    Assert-Alive $Group
    foreach ($child in $Group) {
        $delta = $child.process.TotalProcessorTime.TotalMilliseconds - $before[$child.process.Id]
        if ($delta -le 0 -or -not $child.process.Responding) {
            throw "No responsive CPU progress: $($child.name), PID $($child.process.Id)."
        }
        [ordered]@{ name=$child.name; pid=$child.process.Id; cpuMilliseconds=[math]::Round($delta,3); responding=$child.process.Responding }
    }
}

function Close-Player([object]$Child) {
    Assert-Alive @($Child)
    $requested = $Child.process.CloseMainWindow()
    if (-not $requested) { throw "Could not request normal close: $($Child.name)." }
    if (-not $Child.process.WaitForExit($TimeoutSeconds * 1000)) { throw "Normal close timed out: $($Child.name)." }
    $Child.process.WaitForExit()
    $Child.process.Refresh()
    $code = $Child.process.ExitCode
    $results.Add([ordered]@{ name=($Child.name + '-close'); pid=$Child.process.Id; closeRequested=$requested; exitCode=$code })
    if ($null -eq $code -or $code -ne 0) { throw "Abnormal exit after close: $($Child.name), exit=$code." }
}

try {
    Copy-Item -LiteralPath (Join-Path $sourceRuntime 'RicePlayer.exe') -Destination $runtime
    Get-ChildItem -LiteralPath $sourceRuntime -File -Filter '*.dll' | Copy-Item -Destination $runtime
    foreach ($file in Get-ChildItem -LiteralPath $sourceProject -Recurse -File) {
        if ($file.FullName -match '[\\/]Logs[\\/]' -or $file.Extension -in @('.log','.tmp','.bak')) { continue }
        $relative = $file.FullName.Substring($sourceProject.TrimEnd('\','/').Length + 1)
        $target = Join-Path $project $relative
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
    $binaryHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    $before = Project-Fingerprint $project

    $smoke = @(1..3 | ForEach-Object { Start-Player "smoke-$_" $true })
    Assert-Alive $smoke
    $results.Add([ordered]@{
        name='three-smoke-processes-overlap'; observedUtc=[DateTime]::UtcNow.ToString('o')
        pids=@($smoke | ForEach-Object { $_.process.Id }); allAlive=$true
    })
    foreach ($child in $smoke) {
        if (-not $child.process.WaitForExit($TimeoutSeconds * 1000)) { throw "Smoke timed out: $($child.name)." }
        $child.process.WaitForExit()
        $child.process.Refresh()
        $code = $child.process.ExitCode
        $text = [IO.File]::ReadAllText($child.stdout)
        $confirmed = $text.Contains('Smoke passed: project loaded (120 frames)')
        $renderer = [regex]::Match($text,'Renderer: ([^\r\n]+)').Groups[1].Value
        $results.Add([ordered]@{ name=$child.name; pid=$child.process.Id; exitCode=$code; expectedFrames=120; smokeConfirmed=$confirmed; renderer=$renderer })
        if ($null -eq $code -or $code -ne 0 -or -not $confirmed -or -not $renderer) {
            throw "Smoke failed: $($child.name), exit=$code. See $($child.stdout) and $($child.stderr)."
        }
    }

    $running = @(1..3 | ForEach-Object { Start-Player "running-$_" $false })
    Wait-ForWindows $running
    $results.Add([ordered]@{
        name='three-running-players'; observedUtc=[DateTime]::UtcNow.ToString('o')
        windows=@($running | ForEach-Object { [ordered]@{ pid=$_.process.Id; hwnd=$_.process.MainWindowHandle.ToInt64() } })
        progress=@(Measure-Progress $running)
    })
    # Stop the middle instance, then verify that both other instances keep working.
    Close-Player $running[1]
    $survivors = @($running[0],$running[2])
    $results.Add([ordered]@{ name='survivors-after-middle-close'; closedPid=$running[1].process.Id; progress=@(Measure-Progress $survivors) })
    foreach ($child in $survivors) { Close-Player $child }

    $projectUnchanged = (Project-Fingerprint $project) -eq $before
    if (-not $projectUnchanged) { throw 'Concurrent playback modified the copied saved project.' }
    $passed = $true
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    # Only handles created by this script are used; never terminate by process name.
    foreach ($child in $children) {
        try {
            if (-not $child.process.HasExited) {
                $child.process.Kill()
                $null = $child.process.WaitForExit(5000)
            }
        } catch {
            $passed = $false
            $failure += " Cleanup failed for $($child.name): $($_.Exception.Message)"
        } finally { $child.process.Dispose() }
    }
    [ordered]@{
        passed=$passed; failure=$failure; recordedUtc=[DateTime]::UtcNow.ToString('o')
        os=[Environment]::OSVersion.VersionString; architecture=$env:PROCESSOR_ARCHITECTURE
        logicalProcessors=[Environment]::ProcessorCount; concurrentInstances=3; launchedProcesses=$children.Count
        silentAudio=[bool]$SilentAudio; executableSHA256=$binaryHash; sharedExecutable=$executable
        sharedProject=$project; workingDirectory=$working; projectUnchanged=$projectUnchanged
        tests=$results.ToArray()
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding UTF8
}
if (-not $passed) { throw $failure }
Write-Host "Multi-instance verification passed: $output"
