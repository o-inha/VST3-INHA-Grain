[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $PluginName,

    [string] $BundlePath = "",
    [string] $Version = "",
    [string] $CMakeFile = "CMakeLists.txt",
    [string] $OutputRoot = "release",

    [string[]] $PresetCandidates = @(
        "build/dist/Presets",
        "Presets"
    ),

    [string[]] $DocumentPaths = @(
        "README.md",
        "INSTALL.txt",
        "HOWTO.txt",
        "BUILD-VALIDATION.txt",
        "LICENSE"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspace = (Get-Location).Path

if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = "build/dist/$PluginName.vst3"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $cmakePath = [IO.Path]::GetFullPath((Join-Path $workspace $CMakeFile))
    if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
        throw "Cannot determine the plug-in version: CMake file not found at '$cmakePath'."
    }

    $cmake = Get-Content -LiteralPath $cmakePath -Raw
    $match = [regex]::Match(
        $cmake,
        'project\s*\([^)]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase
    )
    if (-not $match.Success) {
        throw "Cannot determine the plug-in version from project(... VERSION ...) in '$cmakePath'."
    }
    $Version = $match.Groups[1].Value
}

$bundleSource = [IO.Path]::GetFullPath((Join-Path $workspace $BundlePath))
if (-not (Test-Path -LiteralPath $bundleSource -PathType Container)) {
    throw "VST3 bundle not found at '$bundleSource'."
}

$expectedBundleName = "$PluginName.vst3"
$actualBundleName = Split-Path -Leaf $bundleSource
if ($actualBundleName -cne $expectedBundleName) {
    throw "Expected bundle '$expectedBundleName', found '$actualBundleName'."
}

$artifactSlug = (($PluginName.Trim() -replace '[^A-Za-z0-9._-]+', '-').Trim([char] '-'))
$artifactName = "$artifactSlug-v$Version-Windows-x64-VST3"
$stage = [IO.Path]::GetFullPath((Join-Path $workspace (Join-Path $OutputRoot $artifactName)))
if (Test-Path -LiteralPath $stage) {
    throw "Release staging directory already exists: '$stage'."
}
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item -LiteralPath $bundleSource -Destination $stage -Recurse -Force

$presetSource = $null
foreach ($candidate in $PresetCandidates) {
    $candidatePath = [IO.Path]::GetFullPath((Join-Path $workspace $candidate))
    if (Test-Path -LiteralPath $candidatePath -PathType Container) {
        $presetSource = $candidatePath
        break
    }
}
if ($null -ne $presetSource) {
    Copy-Item -LiteralPath $presetSource -Destination $stage -Recurse -Force
}

foreach ($document in $DocumentPaths) {
    $documentPath = [IO.Path]::GetFullPath((Join-Path $workspace $document))
    if (Test-Path -LiteralPath $documentPath -PathType Leaf) {
        Copy-Item -LiteralPath $documentPath -Destination $stage -Force
    }
}

$bundleDestination = Join-Path $stage $expectedBundleName
$binaryDestination = Join-Path $bundleDestination "Contents/x86_64-win/$PluginName.vst3"
if (-not (Test-Path -LiteralPath $bundleDestination -PathType Container)) {
    throw "Packaged VST3 folder is missing: '$bundleDestination'."
}
if (-not (Test-Path -LiteralPath $binaryDestination -PathType Leaf)) {
    throw "Packaged VST3 binary is missing: '$binaryDestination'."
}
if (Test-Path -LiteralPath (Join-Path $stage "Contents")) {
    throw "Invalid package layout: Contents must remain inside '$expectedBundleName'."
}
if ($null -ne $presetSource -and
    -not (Test-Path -LiteralPath (Join-Path $stage "Presets") -PathType Container)) {
    throw "Preset source was found, but the package does not contain a Presets folder."
}

$hash = (Get-FileHash -LiteralPath $binaryDestination -Algorithm SHA256).Hash.ToLowerInvariant()
$binaryInPackage = "$expectedBundleName/Contents/x86_64-win/$PluginName.vst3"
"$hash *$binaryInPackage" |
    Set-Content -LiteralPath (Join-Path $stage "SHA256SUMS.txt") -Encoding ascii

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_OUTPUT)) {
    "artifact_name=$artifactName" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    "stage_path=$stage" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    ("has_presets=$($null -ne $presetSource)").ToLowerInvariant() |
        Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
}

Write-Host "Packaged $expectedBundleName in $stage"
Write-Host "Artifact name: $artifactName"
Write-Host "Presets included: $($null -ne $presetSource)"
