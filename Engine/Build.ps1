param(
    [string]$BuildDirectory = 'Cache/local',
    [string]$FmodRoot = ${env:FMOD_ROOT},
    [string]$InstalledDependencies = '',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Join-Path $env:USERPROFILE 'vcpkg' }
if (-not $FmodRoot) { $FmodRoot = 'C:/Program Files (x86)/FMOD SoundSystem/FMOD Studio API Windows' }
$toolchain = Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake'
if (-not (Test-Path -LiteralPath $toolchain)) { $toolchain = Join-Path $env:VCPKG_ROOT 'vcpkg.cmake' }
if (-not (Test-Path -LiteralPath $toolchain)) { throw 'Set VCPKG_ROOT to an installed vcpkg directory.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ tools are required.' }
$version = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if (-not $version) { throw 'Visual Studio C++ tools are required.' }
$generator = if ($version.StartsWith('18.')) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $source $BuildDirectory }
$argsList = @('-S', $source, '-B', $build, '-G', $generator, '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DFMOD_ROOT=$FmodRoot", '-DBUILD_TESTING=ON')
if ($InstalledDependencies) { $argsList += @('-DVCPKG_MANIFEST_MODE=OFF', "-DVCPKG_INSTALLED_DIR=$InstalledDependencies") }
& cmake @argsList
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build $build --config $Configuration --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
& ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
# Install the three entry points and their private runtime.
$presentationRuntime = Split-Path $source -Parent
& cmake --install $build --config $Configuration --prefix $presentationRuntime
if ($LASTEXITCODE) { throw 'Packaging failed' }
Write-Host "Projects ready: $(Join-Path $presentationRuntime 'Projects')"
Write-Host "Editor ready: $(Join-Path $presentationRuntime 'Rice.exe')"
Write-Host "Local player ready: $(Join-Path $presentationRuntime 'RicePlayer.exe')"
Write-Host "RiceCon ready: $(Join-Path $presentationRuntime 'RiceCon.exe')"
