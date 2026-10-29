param([switch]$RunTests)

$ErrorActionPreference = 'Stop'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'CMake was not found. Install CMake or Visual Studio C++ tools.'
    }
    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $installation) {
        throw 'Visual Studio C++ tools were not found.'
    }
    $cmakePath = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}

$buildDirectory = Join-Path $PSScriptRoot 'build'
& $cmakePath -S $PSScriptRoot -B $buildDirectory
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmakePath --build $buildDirectory --config Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($RunTests) {
    $ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
    & $ctestPath --test-dir $buildDirectory -C Debug --output-on-failure
    exit $LASTEXITCODE
}
