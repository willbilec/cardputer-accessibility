# Without switches, only preview the generated files eligible for removal.
[CmdletBinding(SupportsShouldProcess)]
param([switch]$Apply, [switch]$Interactive)

$ErrorActionPreference = 'Stop'
if ($Apply -and $Interactive) { throw 'Choose -Apply or -Interactive.' }
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
if (-not (Test-Path -LiteralPath (Join-Path $root 'builder/modules.json') -PathType Leaf)) {
    throw "Cardputer checkout not found at $root."
}

$targets = @(
    'workspace/toolchain/.cache/tmp'
    'workspace/toolchain/.cache/downloads'
    'workspace/toolchain/.cache/uv'
    'workspace/toolchain/dist'
    'research/Launcher/.pio/core/.cache/tmp'
    'research/offline-reader/.pio/buildcache'
    'archived/legacy-workspace/research/offline-reader/.pio/buildcache'
    'archived/legacy-workspace/research/offline-reader/.pio/libdeps'
    'archived/legacy-workspace/research/offline-reader/generated'
    'archived/legacy-workspace/research/Launcher/.pio'
    'archived/legacy-workspace/research/speech-prototype/.pio'
    'archived/local-artifacts/previous-builder-venv'
    'archived/local-artifacts/previous-releases'
    'archived/local-artifacts/pre-release-0.2.0'
    'release-files/v0.2.0'
)

$builds = Join-Path $root 'workspace/builds'
if (Test-Path -LiteralPath $builds -PathType Container) {
    $targets += Get-ChildItem -LiteralPath $builds -Directory | Where-Object {
        -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'build.json') -PathType Leaf)
    } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -Skip 2 | ForEach-Object {
        Join-Path 'workspace/builds' $_.Name
    }
}

$prefix = $root + [IO.Path]::DirectorySeparatorChar
$total = 0
$eligible = [System.Collections.Generic.List[string]]::new()
foreach ($relative in $targets) {
    $target = [IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Target outside the Cardputer workspace: $target"
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { continue }
    $resolved = (Resolve-Path -LiteralPath $target).ProviderPath
    if ($resolved -ne $target) { throw "Target resolved to a different path: $resolved" }
    for ($path = $target; $path -ne $root; $path = [IO.Path]::GetDirectoryName($path)) {
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Target is under a link or junction: $path"
        }
    }
    $entries = @(Get-ChildItem -LiteralPath $target -Recurse -Force)
    if ($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1) {
        Write-Warning "Skipped $relative because it contains a link or junction."
        continue
    }
    $size = ($entries | Where-Object { -not $_.PSIsContainer } | Measure-Object -Property Length -Sum).Sum
    $total += $size
    '{0,8:N0} MiB  {1}' -f ($size / 1MB), $relative
    $eligible.Add($target)
}
'{0:N2} GiB of generated files listed' -f ($total / 1GB)
if ($eligible.Count -eq 0) { return }
if ($Interactive) {
    $Apply = (Read-Host 'Close the builder. Type CLEAN to remove the listed folders, or press Enter to cancel') -ceq 'CLEAN'
    if (-not $Apply) { 'Cleanup cancelled.'; return }
}
if (-not $Apply) {
    'Preview only. Run again with -Apply to remove the listed files.'
    return
}
if (Get-Process -Name 'xtensa*','scons*' -ErrorAction SilentlyContinue) {
    throw 'Wait for the compiler to finish before cleaning.'
}
foreach ($target in $eligible) {
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { continue }
    for ($path = $target; $path -ne $root; $path = [IO.Path]::GetDirectoryName($path)) {
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Target changed to a link or junction: $path"
        }
    }
    if (Get-ChildItem -LiteralPath $target -Recurse -Force -Attributes ReparsePoint | Select-Object -First 1) {
        Write-Warning "Skipped $target because it now contains a link or junction."
        continue
    }
    if ($PSCmdlet.ShouldProcess($target, 'Remove generated workspace files')) {
        Remove-Item -LiteralPath $target -Recurse -Force
        "Removed $target"
    }
}
