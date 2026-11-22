# Generates a pack of synthesized soundboard clips (48 kHz stereo WAV)
# and adds them to the VoiceDeck sound library.

$ErrorActionPreference = "Stop"
$sampleRate = 48000
$outDir = "D:\Code\VoiceDeck\build\Release\data\sounds"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Write-Wav($name, $samples, $rate = 48000, $ch = 2) {
    $path = Join-Path $outDir "$name.wav"
    $total = $samples.Count
    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
    $bw.Write([int32](36 + $total * 4))
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
    $bw.Write([int32]16); $bw.Write([int16]1); $bw.Write([int16]$ch)
    $bw.Write([int32]$rate); $bw.Write([int32]($rate * 4))
    $bw.Write([int16]4); $bw.Write([int16]16)
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
    $bw.Write([int32]($total * 4))
    foreach ($s in $samples) {
        $v = [int16]([Math]::Max(-32768, [Math]::Min(32767, $s * 16000)))
        $bw.Write($v); $bw.Write($v)
    }
    $bw.Close()
    $len = [math]::Round($total / $rate, 2)
    Write-Host "  $name.wav ($len s)" -ForegroundColor Green
}

function Make-Tone($freq, $dur, $vol = 0.6, $attackMs = 5, $releaseMs = 50) {
    $n = [int]($dur * $sampleRate)
    $result = New-Object double[] $n
    $attackN = [int]($attackMs * $sampleRate / 1000)
    $releaseN = [int]($releaseMs * $sampleRate / 1000)
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $sampleRate
        $v = $vol * [Math]::Sin(2 * [Math]::PI * $freq * $t)
        if ($i -lt $attackN) { $v *= $i / $attackN }
        $relStart = $n - $releaseN
        if ($i -gt $relStart) { $v *= ($n - $i) / $releaseN }
        $result[$i] = $v
    }
    return $result
}

function Make-Sweep($f1, $f2, $dur, $vol = 0.6) {
    $n = [int]($dur * $sampleRate)
    $result = New-Object double[] $n
    $phase = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $n
        $freq = $f1 + ($f2 - $f1) * $t
        $phase += 2 * [Math]::PI * $freq / $sampleRate
        $v = $vol * [Math]::Sin($phase)
        $env = [Math]::Min($i / ($sampleRate * 0.01), ($n - $i) / ($sampleRate * 0.05))
        $result[$i] = $v * $env
    }
    return $result
}

function Make-Noise($dur, $vol = 0.5, $lowpass = 100) {
    $n = [int]($dur * $sampleRate)
    $result = New-Object double[] $n
    $lastVal = 0.0
    $alpha = 1.0 / $lowpass
    for ($i = 0; $i -lt $n; $i++) {
        $r = Get-Random -Minimum -1.0 -Maximum 1.0
        $lastVal += $alpha * ($r - $lastVal)
        $env = [Math]::Min($i / ($sampleRate * 0.005), ($n - $i) / ($sampleRate * 0.02))
        $result[$i] = $lastVal * $vol * $env
    }
    return $result
}

function Make-Harmonics($base, $dur, $vol = 0.5, [int[]]$harmonics = @(1, 2, 3, 4, 5)) {
    $n = [int]($dur * $sampleRate)
    $result = New-Object double[] $n
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $sampleRate
        $v = 0.0
        foreach ($h in $harmonics) {
            $v += $vol / $h * [Math]::Sin(2 * [Math]::PI * $base * $h * $t)
        }
        $env = [Math]::Min($i / ($sampleRate * 0.01), ($n - $i) / ($sampleRate * 0.1))
        $result[$i] = $v * $env
    }
    return $result
}

function Make-Knock($count = 3, $interval = 0.25, $dur = 0.05, $vol = 0.8) {
    $totalDur = $count * $interval + $dur
    $n = [int]($totalDur * $sampleRate)
    $result = New-Object double[] $n
    for ($c = 0; $c -lt $count; $c++) {
        $start = [int](($c * $interval) * $sampleRate)
        $knockLen = [int]($dur * $sampleRate)
        $phase = 0.0
        for ($i = 0; $i -lt $knockLen; $i++) {
            $idx = $start + $i
            if ($idx -ge $n) { break }
            $t = $i / $sampleRate
            $v = $vol * [Math]::Exp(-$t * 80) * ([Math]::Sin(2 * [Math]::PI * 120 * $t) + 0.5 * [Math]::Sin(2 * [Math]::PI * 80 * $t))
            $result[$idx] = $v
        }
    }
    return $result
}

function Make-Buzzer($dur = 0.8, $freq = 200, $vol = 0.7) {
    $n = [int]($dur * $sampleRate)
    $result = New-Object double[] $n
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $sampleRate
        $square = if ([Math]::Sin(2 * [Math]::PI * $freq * $t) -gt 0) { 1 } else { -1 }
        $result[$i] = $vol * $square
    }
    return $result
}

Write-Host "Generating soundboard pack..." -ForegroundColor Cyan

# 1. Air horn (harmonics + envelope)
Write-Wav "air-horn" (Make-Harmonics 440 1.5 0.7 @(1, 2, 3, 4))

# 2. Bell ding (single freq + long decay)
Write-Wav "bell" (Make-Tone 1760 1.2 0.5 2 400)

# 3. Drum roll (fast noise pulses)
$drumLen = [int](2.0 * $sampleRate)
$drum = New-Object double[] $drumLen
for ($i = 0; $i -lt $drumLen; $i++) {
    $t = $i / $sampleRate
    $beat = [Math]::Floor($t * 12)
    $beatT = $t - ($beat / 12)
    $env = [Math]::Exp(-$beatT * 30)
    $noise = Get-Random -Minimum -1.0 -Maximum 1.0
    $tone = 0.5 * [Math]::Sin(2 * [Math]::PI * 150 * $beatT)
    $drum[$i] = 0.6 * $env * ($noise * 0.5 + $tone)
}
Write-Wav "drum-roll" $drum

# 4. Applause (filtered noise bursts)
$applLen = [int](3.0 * $sampleRate)
$appl = New-Object double[] $applLen
$lastV = 0.0
for ($i = 0; $i -lt $applLen; $i++) {
    $t = $i / $sampleRate
    $r = Get-Random -Minimum -1.0 -Maximum 1.0
    $lastV += 0.05 * ($r - $lastV)
    $burstRate = 8 + 4 * [Math]::Sin($t * 2)
    $burst = [Math]::Abs([Math]::Sin(2 * [Math]::PI * $burstRate * $t))
    $env = [Math]::Min($t / 0.3, ($applLen / $sampleRate - $t) / 0.5)
    $appl[$i] = $lastV * $burst * $env * 0.8
}
Write-Wav "applause" $appl

# 5. Explosion (noise burst with low-pass sweep)
$expLen = [int](1.5 * $sampleRate)
$exp = New-Object double[] $expLen
$lastV = 0.0
for ($i = 0; $i -lt $expLen; $i++) {
    $t = $i / $sampleRate
    $r = Get-Random -Minimum -1.0 -Maximum 1.0
    $alpha = if ($t -lt 0.1) { 0.5 } else { 0.02 + 0.48 * [Math]::Exp(-$t * 8) }
    $lastV += $alpha * ($r - $lastV)
    $env = [Math]::Exp(-$t * 3) * [Math]::Min($t / 0.005, 1)
    $subBass = 0.3 * [Math]::Sin(2 * [Math]::PI * 60 * $t) * [Math]::Exp(-$t * 2)
    $exp[$i] = ($lastV * 0.7 + $subBass) * $env
}
Write-Wav "explosion" $exp

# 6. Cash register (bright metallic tones)
$cashLen = [int](0.5 * $sampleRate)
$cash = New-Object double[] $cashLen
for ($i = 0; $i -lt $cashLen; $i++) {
    $t = $i / $sampleRate
    $v = 0.0
    if ($t -lt 0.05) {
        $v = 0.5 * [Math]::Sin(2 * [Math]::PI * 2000 * $t) * [Math]::Exp(-$t * 50)
    }
    if ($t -gt 0.1 -and $t -lt 0.2) {
        $v = 0.6 * [Math]::Sin(2 * [Math]::PI * 1500 * ($t - 0.1)) * [Math]::Exp(-($t - 0.1) * 30)
    }
    $cash[$i] = $v
}
Write-Wav "cash-register" $cash

# 7. Gong (complex harmonics, long decay)
Write-Wav "gong" (Make-Harmonics 100 4.0 0.6 @(1, 1.5, 2, 2.5, 3, 4, 5))

# 8. Wrong answer (descending tones)
$wrongLen = [int](0.8 * $sampleRate)
$wrong = New-Object double[] $wrongLen
for ($i = 0; $i -lt $wrongLen; $i++) {
    $t = $i / $sampleRate
    $v = 0.0
    if ($t -lt 0.35) {
        $v = 0.5 * [Math]::Sin(2 * [Math]::PI * 400 * $t)
    } elseif ($t -lt 0.4) {
        # brief silence
    } else {
        $v = 0.5 * [Math]::Sin(2 * [Math]::PI * 300 * ($t - 0.4)) * [Math]::Exp(-($t - 0.4) * 3)
    }
    $wrong[$i] = $v
}
Write-Wav "wrong" $wrong

# 9. Wow (rising sweep)
Write-Wav "wow" (Make-Sweep 200 1200 0.5 0.6)

# 10. Knock (three knocks)
Write-Wav "knock" (Make-Knock 3 0.3 0.06 0.8)

# 11. Sad trombone (descending pitch wobble)
$sadLen = [int](2.0 * $sampleRate)
$sad = New-Object double[] $sadLen
$phase = 0.0
for ($i = 0; $i -lt $sadLen; $i++) {
    $t = $i / $sampleRate
    $freq = 250 - 150 * $t
    if ($freq -lt 80) { $freq = 80 }
    $freq += 15 * [Math]::Sin(2 * [Math]::PI * 5 * $t)
    $phase += 2 * [Math]::PI * $freq / $sampleRate
    $env = [Math]::Min($t / 0.05, ($sadLen / $sampleRate - $t) / 0.3)
    $sad[$i] = 0.5 * [Math]::Sin($phase) * $env + 0.2 * [Math]::Sin($phase * 2) * $env
}
Write-Wav "sad-trombone" $sad

# 12. Crickets (chirp bursts)
$crickLen = [int](3.0 * $sampleRate)
$crick = New-Object double[] $crickLen
for ($i = 0; $i -lt $crickLen; $i++) {
    $t = $i / $sampleRate
    $v = 0.0
    $chirpPhase = ($t * 3.0) % 1.0
    if ($chirpPhase -lt 0.15) {
        $freq = 4000 + 500 * [Math]::Sin($chirpPhase * 50)
        $v = 0.3 * [Math]::Sin(2 * [Math]::PI * $freq * $t)
    }
    $crick[$i] = $v
}
Write-Wav "crickets" $crick

# 13. Buzzer (game show wrong answer)
Write-Wav "buzzer" (Make-Buzzer 1.0 220 0.6)

# 14. Whoosh (filtered noise sweep)
$whooshLen = [int](0.8 * $sampleRate)
$whoosh = New-Object double[] $whooshLen
$lastV = 0.0
for ($i = 0; $i -lt $whooshLen; $i++) {
    $t = $i / $sampleRate
    $r = Get-Random -Minimum -1.0 -Maximum 1.0
    $alpha = 0.1 + 0.4 * (1 - $t / 0.8)
    $lastV += $alpha * ($r - $lastV)
    $env = [Math]::Sin([Math]::PI * $t / 0.8)
    $whoosh[$i] = $lastV * 0.7 * $env
}
Write-Wav "whoosh" $whoosh

# 15. Heartbeat (two-thump pattern)
$heartLen = [int](2.0 * $sampleRate)
$heart = New-Object double[] $heartLen
for ($i = 0; $i -lt $heartLen; $i++) {
    $t = $i / $sampleRate
    $beatPhase = ($t * 1.2) % 1.0
    $v = 0.0
    if ($beatPhase -lt 0.1) {
        $v = 0.8 * [Math]::Sin(2 * [Math]::PI * 60 * $beatPhase) * [Math]::Exp(-$beatPhase * 30)
    }
    elseif ($beatPhase -ge 0.15 -and $beatPhase -lt 0.25) {
        $v = 0.6 * [Math]::Sin(2 * [Math]::PI * 50 * ($beatPhase - 0.15)) * [Math]::Exp(-($beatPhase - 0.15) * 30)
    }
    $heart[$i] = $v
}
Write-Wav "heartbeat" $heart

# 16. Fanfare (ascending triad)
$fanLen = [int](1.5 * $sampleRate)
$fan = New-Object double[] $fanLen
$notes = @(262, 330, 392, 523)  # C E G C
for ($i = 0; $i -lt $fanLen; $i++) {
    $t = $i / $sampleRate
    $noteIdx = [Math]::Floor($t * 4)
    if ($noteIdx -ge 3) { $noteIdx = 3; $noteT = $t - (3 * 0.25) }
    else { $noteT = $t - ($noteIdx * 0.25) }
    if ($noteT -lt 0) { $noteT = 0 }
    $freq = $notes[$noteIdx]
    $env = if ($noteIdx -lt 3) { [Math]::Min($noteT / 0.01, 1) } else { [Math]::Min($noteT / 0.01, ($fanLen / $sampleRate - $t) / 0.3) }
    $v = 0.6 * [Math]::Sin(2 * [Math]::PI * $freq * $t) * $env
    $v += 0.3 * [Math]::Sin(2 * [Math]::PI * $freq * 2 * $t) * $env
    $fan[$i] = $v
}
Write-Wav "fanfare" $fan

# 17. Boing (spring sound)
$boingLen = [int](1.0 * $sampleRate)
$boing = New-Object double[] $boingLen
$phase = 0.0
for ($i = 0; $i -lt $boingLen; $i++) {
    $t = $i / $sampleRate
    $freq = 300 + 200 * [Math]::Exp(-$t * 5) * [Math]::Sin(2 * [Math]::PI * 10 * $t)
    $phase += 2 * [Math]::PI * $freq / $sampleRate
    $env = [Math]::Exp(-$t * 2.5)
    $boing[$i] = 0.5 * [Math]::Sin($phase) * $env
}
Write-Wav "boing" $boing

# 18. Laser (descending sweep, fast)
Write-Wav "laser" (Make-Sweep 2000 100 0.3 0.7)

# 19. Level up (ascending arpeggio)
$levelLen = [int](1.2 * $sampleRate)
$level = New-Object double[] $levelLen
$levelNotes = @(523, 659, 784, 1047)  # C5 E5 G5 C6
for ($i = 0; $i -lt $levelLen; $i++) {
    $t = $i / $sampleRate
    $noteIdx = [Math]::Floor($t * 4)
    if ($noteIdx -ge 3) { $noteIdx = 3; $noteT = $t - 0.75 }
    else { $noteT = $t - ($noteIdx * 0.25) }
    $freq = $levelNotes[$noteIdx]
    $env = if ($noteIdx -lt 3) { [Math]::Min($noteT / 0.01, 1) } else { [Math]::Min($noteT / 0.01, ($levelLen / $sampleRate - $t) / 0.3) }
    $v = 0.5 * [Math]::Sin(2 * [Math]::PI * $freq * $t) * $env
    $v += 0.2 * [Math]::Sin(2 * [Math]::PI * $freq * 2 * $t) * $env
    $level[$i] = $v
}
Write-Wav "level-up" $level

# 20. Dun dun dunnn (dramatic three-note)
$dunLen = [int](3.0 * $sampleRate)
$dun = New-Object double[] $dunLen
$dunNotes = @(220, 208, 196)  # A3 G#3 G3
for ($i = 0; $i -lt $dunLen; $i++) {
    $t = $i / $sampleRate
    $noteIdx = [Math]::Floor($t)
    if ($noteIdx -gt 2) { $noteIdx = 2 }
    $noteT = $t - $noteIdx
    if ($noteT -lt 0) { $noteT = 0 }
    $freq = $dunNotes[$noteIdx]
    $env = [Math]::Exp(-$noteT * 1.5)
    $v = 0.6 * [Math]::Sin(2 * [Math]::PI * $freq * $t) * $env
    $v += 0.3 * [Math]::Sin(2 * [Math]::PI * $freq * 2 * $t) * $env
    $v += 0.1 * [Math]::Sin(2 * [Math]::PI * $freq * 0.5 * $t) * $env
    $dun[$i] = $v
}
Write-Wav "dun-dun-dun" $dun

Write-Host "`nAll sounds generated!" -ForegroundColor Cyan
Write-Host "Files in: $outDir" -ForegroundColor Yellow
(Get-ChildItem $outDir -Filter *.wav).Count.ToString() + " WAV files total"
