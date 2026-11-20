Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$packager = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../scripts/PackageWindowsVST3.ps1"))
$fixtureRoot = Join-Path $env:RUNNER_TEMP "inha-package-tests-$PID"

function Invoke-Fixture([string] $pluginName, [bool] $withPresets) {
    $workspace = Join-Path $fixtureRoot ($pluginName -replace ' ', '-')
    $bundle = Join-Path $workspace "build/dist/$pluginName.vst3"
    $binary = Join-Path $bundle "Contents/x86_64-win/$pluginName.vst3"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $binary) | Out-Null
    Set-Content -LiteralPath $binary -Value "fixture-binary" -Encoding ascii
    Set-Content -LiteralPath (Join-Path $workspace "CMakeLists.txt") `
        -Value "project(Fixture VERSION 9.8.7 LANGUAGES CXX)" -Encoding ascii
    Set-Content -LiteralPath (Join-Path $workspace "README.md") `
        -Value "Fixture documentation" -Encoding utf8

    if ($withPresets) {
        $preset = Join-Path $workspace "Presets/INHA/$pluginName/Default.vstpreset"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $preset) | Out-Null
        Set-Content -LiteralPath $preset -Value "fixture-preset" -Encoding ascii
    }

    Push-Location $workspace
    try {
        & $packager -PluginName $pluginName
    }
    finally {
        Pop-Location
    }

    $slug = $pluginName -replace ' ', '-'
    $stage = Join-Path $workspace "release/$slug-v9.8.7-Windows-x64-VST3"
    $packagedBundle = Join-Path $stage "$pluginName.vst3"
    if (-not (Test-Path -LiteralPath $packagedBundle -PathType Container)) {
        throw "Fixture package is missing its outer .vst3 folder."
    }
    if (Test-Path -LiteralPath (Join-Path $stage "Contents")) {
        throw "Fixture package incorrectly exposes Contents at its root."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $stage "README.md") -PathType Leaf)) {
        throw "Fixture package is missing optional customer documentation."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $stage "SHA256SUMS.txt") -PathType Leaf)) {
        throw "Fixture package is missing SHA256SUMS.txt."
    }

    $packagedPresets = Test-Path -LiteralPath (Join-Path $stage "Presets") -PathType Container
    if ($packagedPresets -ne $withPresets) {
        throw "Fixture preset inclusion does not match the source repository."
    }
}

try {
    Invoke-Fixture "INHA With Presets" $true
    Invoke-Fixture "INHA Without Presets" $false
    Write-Host "Windows VST3 packaging tests passed."
}
finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
