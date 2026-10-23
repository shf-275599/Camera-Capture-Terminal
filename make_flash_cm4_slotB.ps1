[CmdletBinding()]
param(
    [ValidateRange(0, 256)]
    [int]$Jobs = 0,
    [uint32]$Version = 0
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'make_flash_core.ps1') -Core CM4 -Slot B -Jobs $Jobs -Version $Version
