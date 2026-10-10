<#
Create a local experimental CM2450E auth config from the user's own
INSITECallTrace v1.3 sensitive buffer file.
NO credentials are printed, transmitted, or committed.
The 32-byte field is copied from the capture; fresh-session rules are unknown.
#>
param(
    [Parameter(Mandatory = $true)] [string] $TraceFile,
    [Parameter(Mandatory = $true)] [string] $OutputFile
)
$ErrorActionPreference = "Stop"
$records = New-Object System.Collections.ArrayList
$current = $null
foreach ($line in [System.IO.File]::ReadLines((Resolve-Path -LiteralPath $TraceFile))) {
    if ($line -match '^EVENT=\d+\s+T_ms=\d+\s+tid=\d+\s+(\S+)\s+address=') {
        if ($null -ne $current) { throw "Unterminated capture record." }
        $current = @{ Kind = $Matches[1]; Hex = "" }
        continue
    }
    if ($null -eq $current) { continue }
    if ($line -match '^\s+\+0x[0-9A-Fa-f]+:\s+((?:[0-9A-Fa-f]{2}\s*)+)$') {
        $bytes = [regex]::Matches($Matches[1], '[0-9A-Fa-f]{2}')
        foreach ($b in $bytes) { $current.Hex += $b.Value.ToUpperInvariant() }
    } elseif ($line -eq "END") {
        [void] $records.Add($current)
        $current = $null
    }
}
if ($null -ne $current) { throw "Truncated capture file." }
$plain = @($records | Where-Object { $_.Kind -eq 'TRANSFORM_INPUT' } | Select-Object -First 1)
$key = @($records | Where-Object { $_.Kind -eq 'AES_CBC_KEY_16' } | Select-Object -First 1)
if ($plain.Count -ne 1 -or $key.Count -ne 1) {
    throw "Requires v1.3 capture with TRANSFORM_INPUT and AES_CBC_KEY_16."
}
if ($plain[0].Hex.Length -ne 198 -or $key[0].Hex.Length -ne 32) {
    throw "Unexpected capture lengths. Refusing to export."
}
$context = $plain[0].Hex.Substring(32, 102)
$opaque32 = $plain[0].Hex.Substring(134, 64)
$lines = @(
    "# Sensitive local test material; NEVER commit or share this file",
    "KEY_HEX=$($key[0].Hex)",
    "CONTEXT_HEX=$context",
    "OPAQUE32_HEX=$opaque32"
)
$full = [System.IO.Path]::GetFullPath($OutputFile)
[System.IO.File]::WriteAllLines($full, $lines, [System.Text.Encoding]::ASCII)
Write-Host "Wrote local CLIP46 test configuration to $full (secrets not displayed)."
Write-Warning "Captured opaque32 may not be valid for a fresh ECC session. Test on authorized bench ECM only."
