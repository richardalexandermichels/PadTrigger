param([Parameter(Mandatory)][string] $PackagePath)

$ErrorActionPreference = 'Stop'
$classId = 'ABCDEF019182FAEB4D79767350647472'
$bundle = Join-Path $PackagePath 'Pad Trigger.vst3'
$binary = Join-Path $bundle 'Contents/x86_64-win/Pad Trigger.vst3'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw 'The Windows x64 VST3 binary is missing.'
}

$manifest = Get-Content -LiteralPath (Join-Path $bundle 'Contents/Resources/moduleinfo.json') -Raw | ConvertFrom-Json
$effect = @($manifest.Classes | Where-Object { $_.Category -eq 'Audio Module Class' })
if ($effect.Count -ne 1 -or $effect[0].CID -cne $classId -or $effect[0].Name -cne 'Padomizer') {
    throw 'The scanned VST3 identity has changed.'
}

$metadata = Get-Content -LiteralPath (Join-Path $PackagePath 'padomizer-Release-build.txt') -Raw
if ($metadata -notmatch '(?m)^JUCE: [0-9a-f]{40} \(') {
    throw 'The release package does not identify the resolved JUCE revision.'
}

$templates = @{
    'Padomizer - One-track MIDI'           = '(?m)^\s*REC 0 0 2 4 0 0 0 1\s*$'
    'Padomizer - Single drum note'         = '(?m)^\s*REC 0 0 2 4 0 0 0 1\s*$'
    'PadTrigger - One-track stereo'       = '(?m)^\s*REC 0 0 2 3 0 0 0 2\s*$'
    'PadTrigger - sforzando Stereo Print' = '(?m)^\s*REC 0 -1 1 3 0 0 0 2\s*$'
}
foreach ($template in $templates.GetEnumerator()) {
    $file = Join-Path $PackagePath "TrackTemplates/$($template.Key).RTrackTemplate"
    $text = Get-Content -LiteralPath $file -Raw
    if (-not $text.Contains($classId) -or $text -notmatch $template.Value) {
        throw "Unexpected plug-in identity or recording tap in $($template.Key)."
    }
}

foreach ($path in @('README.md', 'DESIGN.md', 'Assets/DSEG-LICENSE.txt', 'Scripts/PadTrigger - Record audio.lua',
                    'Scripts/PadTrigger - Set up stereo print.lua')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackagePath $path) -PathType Leaf)) {
        throw "Missing release file $path."
    }
}

Write-Output 'Verified the Windows x64 bundle, VST3 identity, recording taps, scripts, and build metadata.'
