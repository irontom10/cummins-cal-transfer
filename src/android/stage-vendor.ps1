param(
    [Parameter(Mandatory = $true)]
    [string]$Archive,

    [Parameter(Mandatory = $true)]
    [string]$StageRoot
)

$ErrorActionPreference = "Stop"

$extractRoot = Join-Path $StageRoot "_extract"
$jniRoot = Join-Path $StageRoot "app\src\main\jniLibs"
$assetRoot = Join-Path $StageRoot "app\src\main\assets\Files"

if (Test-Path $extractRoot) {
    Remove-Item -Recurse -Force $extractRoot
}

New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
New-Item -ItemType Directory -Force -Path $jniRoot | Out-Null
New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null

Expand-Archive -LiteralPath $Archive -DestinationPath $extractRoot -Force

@("arm64-v8a", "armeabi-v7a") | ForEach-Object {
    $abi = $_
    $destination = Join-Path $jniRoot $abi
    New-Item -ItemType Directory -Force -Path $destination | Out-Null

    foreach ($library in @(
        "libc++_shared.so",
        "libnuln2r32.so",
        "libnuln3r32.so",
        "libnblr32.so",
        "libnbl2r32.so",
        "libcil7r32.so",
        "libcimr32.so",
        "libcim16r32.so",
        "libculn3r32.so",
        "libkuln3r32.so"
    )) {
        $match = Get-ChildItem -LiteralPath $extractRoot -Recurse -File |
            Where-Object { $_.Name -ieq $library -and $_.Directory.Name -ieq $abi } |
            Select-Object -First 1

        if ($null -eq $match) {
            throw "Could not find $library for $abi in the NEXIQ SDK."
        }

        Copy-Item -LiteralPath $match.FullName -Destination (Join-Path $destination $library) -Force
    }
}

$iniAnchor = Get-ChildItem -LiteralPath $extractRoot -Recurse -File |
    Where-Object { $_.Name -ieq "nuln3r32.ini" } |
    Select-Object -First 1

if ($null -eq $iniAnchor) {
    throw "Could not find nuln3r32.ini in the NEXIQ SDK."
}

Copy-Item -Path (Join-Path $iniAnchor.Directory.FullName "*") -Destination $assetRoot -Recurse -Force

Remove-Item -Recurse -Force $extractRoot
Remove-Item -Force $Archive

Write-Host "[android] NEXIQ RP1210 payload staged from official SDK."
