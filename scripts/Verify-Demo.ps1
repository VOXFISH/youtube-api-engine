param(
    [string]$PackageRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$OutputDirectory = '',
    [string]$MesaDirectory = '',
    [ValidateRange(10,600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess) {
    throw 'The packaged demo requires a 64-bit Windows PowerShell process.'
}
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $package ('Build/portability/run-' + [guid]::NewGuid().ToString('N')) }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Choose a new output directory; verification never deletes existing files.' }
$runtime = Join-Path $output 'Runtime with spaces'
$working = Join-Path $output 'Different working directory'
$project = Join-Path $output 'Projects/Grass Demo'
$steveProject = Join-Path $output 'Projects/Steve Demo'
$results = [Collections.Generic.List[object]]::new()
foreach ($directory in @($output,$runtime,$working)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

function Copy-Runtime([string]$Source,[string]$Destination,[string[]]$Executables) {
    foreach ($name in $Executables) {
        $file = Join-Path $Source $name
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing packaged executable: $file" }
        Copy-Item -LiteralPath $file -Destination $Destination
    }
    Get-ChildItem -LiteralPath $Source -File -Filter '*.dll' | Copy-Item -Destination $Destination
    if ($MesaDirectory) {
        # Mesa is injected only into the disposable test copy, never the delivered editor.
        foreach ($name in @('opengl32.dll','libgallium_wgl.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $MesaDirectory $name) -PathType Leaf)) {
                throw "Missing Mesa desktop OpenGL library: $name"
            }
        }
        Get-ChildItem -LiteralPath $MesaDirectory -File -Filter '*.dll' | Copy-Item -Destination $Destination
    }
}

function Copy-Project([string]$Source,[string]$Destination) {
    if (-not (Test-Path -LiteralPath (Join-Path $Source 'project.rice') -PathType Leaf)) { throw "Missing demo project: $Source" }
    foreach ($file in Get-ChildItem -LiteralPath $Source -Recurse -File) {
        if ($file.FullName -match '[\\/]Logs[\\/]' -or $file.Extension -in @('.log','.tmp','.bak')) { continue }
        $relative = $file.FullName.Substring($Source.TrimEnd('\','/').Length + 1)
        # Copy only playable project data; videos, previews and documentation
        # are presentation deliverables, not runtime dependencies.
        if ($relative -notin @('project.rice','main.rice') -and $relative -notmatch '^(Assets|Scenes)[\\/]') { continue }
        $target = Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}

function Project-Fingerprint([string]$Directory) {
    return ((Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Directory.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join "`n")
}

function Invoke-Smoke([string]$Name,[string]$Executable,[string[]]$ExtraArguments = @(),[string]$ExpectedState = '') {
    $stdout = Join-Path $output ($Name + '.stdout.txt')
    $stderr = Join-Path $output ($Name + '.stderr.txt')
    $arguments = @('--smoke','--silent-audio') + $ExtraArguments
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $working `
        -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) { $process.Kill(); $process.WaitForExit() }
    $process.Refresh()
    $exitCode = $process.ExitCode
    $watch.Stop()
    $outputText = [IO.File]::ReadAllText($stdout)
    $startupConfirmed = -not $ExpectedState -or $outputText.Contains("Smoke passed: $ExpectedState (120 frames)")
    $renderer = [regex]::Match($outputText,'Renderer: ([^\r\n]+)').Groups[1].Value
    $softwareRendererConfirmed = -not ($MesaDirectory -and $ExpectedState) -or $renderer -match 'llvmpipe'
    $results.Add([ordered]@{
        name=$Name; exitCode=$exitCode; timedOut=$timedOut; seconds=[math]::Round($watch.Elapsed.TotalSeconds,3)
        executableSHA256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
        expectedState=$ExpectedState; startupConfirmed=$startupConfirmed; renderer=$renderer
        softwareRendererConfirmed=$softwareRendererConfirmed
    })
    Write-Host "$Name : exit=$exitCode, $([math]::Round($watch.Elapsed.TotalSeconds,2)) seconds"
    if ($timedOut -or $null -eq $exitCode -or $exitCode -ne 0) {
        if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr | Write-Host }
        throw "Smoke failed: $Name (exit=$exitCode, timeout=$timedOut)"
    }
    if (-not $startupConfirmed -or -not $softwareRendererConfirmed) {
        Write-Host $outputText
        throw "Smoke output did not confirm the expected state/renderer: $Name"
    }
}

$savedEnvironment = @{}
foreach ($name in @('PATH','VCPKG_ROOT','FMOD_ROOT','GALLIUM_DRIVER','LIBGL_ALWAYS_SOFTWARE','LP_NUM_THREADS')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
$passed = $false
try {
    Copy-Runtime $package $runtime @('RiceEditor.exe','RicePlayer.exe')
    # A clean app directory and restricted PATH prevent use of SDK/build-tree DLLs.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT',$null,'Process')
    [Environment]::SetEnvironmentVariable('FMOD_ROOT',$null,'Process')
    if ($MesaDirectory) {
        $env:GALLIUM_DRIVER = 'llvmpipe'
        $env:LIBGL_ALWAYS_SOFTWARE = 'true'
        $env:LP_NUM_THREADS = '2'
    }
    Invoke-Smoke 'editor-empty' (Join-Path $runtime 'RiceEditor.exe') @() 'empty editor'
    foreach ($base in @($runtime,$working)) {
        foreach ($name in @('Project','Assets','Logs')) {
            if (Test-Path -LiteralPath (Join-Path $base $name)) { throw "Empty editor created unexpected $name in $base" }
        }
    }
    Copy-Project (Join-Path $package 'Presentation/Grass1000') $project
    Copy-Project (Join-Path $package 'Presentation/Steve') $steveProject
    # Match the top-level double-click layout as well as explicit project paths.
    $directSteve = Join-Path $runtime 'Presentation/Steve'
    Copy-Project (Join-Path $package 'Presentation/Steve') $directSteve
    foreach ($demo in @(@{Name='grass';Path=$project},@{Name='steve';Path=$steveProject})) {
        $before = Project-Fingerprint $demo.Path
        Invoke-Smoke ('editor-' + $demo.Name) (Join-Path $runtime 'RiceEditor.exe') @('--project',('"{0}"' -f (Join-Path $demo.Path 'project.rice'))) 'project loaded'
        Invoke-Smoke ('player-' + $demo.Name) (Join-Path $runtime 'RicePlayer.exe') @('--project',('"{0}"' -f $demo.Path)) 'project loaded'
        if ((Project-Fingerprint $demo.Path) -ne $before) { throw "Playing the copied $($demo.Name) demo modified saved project data." }
    }
    if (Get-ChildItem -LiteralPath $output -Recurse -Directory -Filter 'Logs') { throw 'Unarmed smoke runs created function log directories.' }
    $directBefore = Project-Fingerprint $directSteve
    Invoke-Smoke 'player-direct-start' (Join-Path $runtime 'RicePlayer.exe') @() 'project loaded'
    if ((Project-Fingerprint $directSteve) -ne $directBefore) { throw 'Direct playback modified the presentation project.' }
    $passed = $true
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
    [ordered]@{
        passed=$passed; os=[Environment]::OSVersion.VersionString; architecture=$env:PROCESSOR_ARCHITECTURE
        softwareOpenGL=[bool]$MesaDirectory; pathRestrictedToWindows=$true; noSdkRequired=$true
        testCount=$results.Count; tests=$results.ToArray()
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding UTF8
}
Write-Host "Portable demo verification passed: $output"
