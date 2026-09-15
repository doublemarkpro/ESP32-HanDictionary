param(
    [Parameter(Mandatory = $true)]
    [string]$Ffmpeg,
    [Parameter(Mandatory = $true)]
    [string]$AePronunciationRepo
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$header = Join-Path $root 'main\han_dictionary\phonetics.h'
$outputRoot = Join-Path $root 'content\sdcard\handict\phonetics'
$audioRoot = Join-Path $outputRoot 'en-GB'
$rawRoot = Join-Path $AePronunciationRepo 'app\src\main\res\raw'
$license = Join-Path $AePronunciationRepo 'LICENSE'

if (-not (Test-Path -LiteralPath $Ffmpeg -PathType Leaf)) {
    throw "ffmpeg not found: $Ffmpeg"
}
if (-not (Test-Path -LiteralPath $rawRoot -PathType Container)) {
    throw "aePronunciation audio directory not found: $rawRoot"
}

$soundFiles = @{
    'i-long' = 'single_i.mp3'; 'i-short' = 'single_i_short.mp3'; 'e' = 'single_e.mp3'
    'ae' = 'single_ae.mp3'; 'wedge' = 'single_v_upsidedown.mp3'; 'a-long' = 'single_a.mp3'
    'o-short' = 'single_c_backwards.mp3'; 'o-long' = 'single_c_backwards.mp3'
    'u-short' = 'single_u_short.mp3'; 'u-long' = 'single_u.mp3'
    'er-long' = 'single_er_stressed.mp3'; 'schwa' = 'single_shwua.mp3'
    'ei' = 'single_ei.mp3'; 'ai' = 'single_ai.mp3'; 'oi' = 'single_oi.mp3'
    'au' = 'single_au.mp3'; 'ou' = 'single_ou.mp3'; 'ia' = 'single_ir.mp3'
    'ea' = 'single_er.mp3'; 'ua' = 'single_or.mp3'
    'p' = 'single_p.mp3'; 'b' = 'single_b.mp3'; 't' = 'single_t.mp3'
    'd' = 'single_d.mp3'; 'k' = 'single_k.mp3'; 'g' = 'single_g.mp3'
    'f' = 'single_f.mp3'; 'v' = 'single_v.mp3'; 'th' = 'single_th_voiceless.mp3'
    'dh' = 'single_th_voiced.mp3'; 's' = 'single_s.mp3'; 'z' = 'single_z.mp3'
    'sh' = 'single_sh.mp3'; 'zh' = 'single_zh.mp3'; 'h' = 'single_h.mp3'
    'ch' = 'single_ch.mp3'; 'jh' = 'single_dzh.mp3'; 'm' = 'single_m.mp3'
    'n' = 'single_n.mp3'; 'ng' = 'single_ng.mp3'; 'l' = 'single_l.mp3'
    'r' = 'single_r.mp3'; 'w' = 'single_w.mp3'; 'y' = 'single_j.mp3'
}

$curriculum = @()
$source = Get-Content -LiteralPath $header -Raw
$pattern = '\{"([^"]+)",\s*"([^"]+)",\s*\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\},\s*([0-2])\}'
foreach ($match in [regex]::Matches($source, $pattern)) {
    $curriculum += [pscustomobject]@{
        Id = $match.Groups[1].Value
        Ipa = $match.Groups[2].Value
        Words = @($match.Groups[3].Value, $match.Groups[4].Value, $match.Groups[5].Value)
    }
}
if ($curriculum.Count -ne 44) {
    throw "Expected 44 phonetic sounds, found $($curriculum.Count)"
}

function Convert-ToDeviceOpus {
    param([string]$Source, [string]$Destination)
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    & $Ffmpeg -hide_banner -loglevel error -y -i $Source -map_metadata -1 -ac 1 -ar 24000 `
        -af 'loudnorm=I=-18:TP=-2:LRA=7' -c:a libopus -b:a 32k -application voip $Destination
    if ($LASTEXITCODE -ne 0) {
        throw "Audio conversion failed: $Source"
    }
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'han-phonetics-tts-v1'
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
Add-Type -AssemblyName System.Speech
$speaker = [System.Speech.Synthesis.SpeechSynthesizer]::new()
$speaker.SelectVoice('Microsoft Zira Desktop')
$speaker.Rate = -1
$speaker.Volume = 100

$manifest = [ordered]@{
    schema_version = 1
    format = 'Ogg/Opus, mono, 24 kHz input sample rate, 32 kbps, EBU R128 normalized'
    isolated_sounds = [ordered]@{
        source = 'aePronunciation'
        repository = 'https://github.com/suragch/aePronunciation'
        license = 'GPL-3.0'
    }
    example_words = [ordered]@{
        source = 'Microsoft Zira Desktop speech synthesizer on the local Windows development host'
        note = 'Generated locally without playing through the speakers.'
    }
    files = @()
}

try {
    $uniqueWords = $curriculum.Words | Sort-Object -Unique
    $wordWavs = @{}
    $wordNumber = 0
    foreach ($word in $uniqueWords) {
        $wordNumber++
        $wave = Join-Path $temporaryRoot ($word + '.wav')
        $speaker.SetOutputToWaveFile($wave)
        $speaker.Speak($word)
        $speaker.SetOutputToNull()
        $wordWavs[$word] = $wave
        Write-Output "Synthesized word $wordNumber/$($uniqueWords.Count): $word"
    }

    foreach ($sound in $curriculum) {
        $soundSource = Join-Path $rawRoot $soundFiles[$sound.Id]
        if (-not (Test-Path -LiteralPath $soundSource -PathType Leaf)) {
            throw "Missing isolated sound source for $($sound.Id): $soundSource"
        }
        $soundDestination = Join-Path $audioRoot "$($sound.Id)\sound.ogg"
        Convert-ToDeviceOpus -Source $soundSource -Destination $soundDestination
        $manifest.files += "en-GB/$($sound.Id)/sound.ogg"
        foreach ($word in $sound.Words) {
            $wordDestination = Join-Path $audioRoot "$($sound.Id)\$word.ogg"
            Convert-ToDeviceOpus -Source $wordWavs[$word] -Destination $wordDestination
            $manifest.files += "en-GB/$($sound.Id)/$word.ogg"
        }
    }
}
finally {
    $speaker.Dispose()
}

Copy-Item -LiteralPath $license -Destination (Join-Path $outputRoot 'LICENSE-AEPRONUNCIATION-GPL-3.0.txt') -Force
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputRoot 'AUDIO_SOURCES.json') -Encoding utf8
Write-Output "Generated $($manifest.files.Count) audio files in $audioRoot"
