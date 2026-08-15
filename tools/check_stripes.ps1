# check_stripes.ps1 — P12 抗锯齿重采样验证：解析 lwmap，检查
#   ① 逐行海陆占比的行交替相关性（"横纹"信号）；
#   ② 三角正/反格海陆不一致率 + 正/反各自海陆占比差（"同朝向区"信号）。
# 注意：变量名大小写不敏感——勿用 $B/$G/$R 覆盖字节数组。
# 用法: powershell -File tools/check_stripes.ps1 [-Seeds 42,43,44] [-Fc]   （-Fc = 探针 forceCoast 图）
param([int[]]$Seeds = @(42, 43, 44), [switch]$Fc)

function Read-Lwmap([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    if ($magic -ne 'LWMP') { throw "bad magic $magic in $Path" }
    $tiling = $bytes[5]
    $cols = [BitConverter]::ToUInt32($bytes, 6)
    $rows = [BitConverter]::ToUInt32($bytes, 10)
    $per = 1; if ($tiling -eq 2) { $per = 2 }
    $n = [int]($cols * $rows * $per)
    $off = 14
    $land = [bool[]]::new($n)
    for ($i = 0; $i -lt $n; $i++) {
        $bCh = $bytes[$off + $i * 3]; $gCh = $bytes[$off + $i * 3 + 1]; $rCh = $bytes[$off + $i * 3 + 2]
        $land[$i] = ($bCh -ge 32 -and $gCh -ge 32 -and $rCh -ge 32)
    }
    return @{ tiling = $tiling; cols = [int]$cols; rows = [int]$rows; land = $land }
}

foreach ($seed in $Seeds) {
    foreach ($tt in @('hex', 'tri')) {
        $f = if ($Fc) { "userdata\maps\probe_${seed}_${tt}_fc.lwmap" } else { "userdata\maps\gen_${seed}_${tt}_105x95_0.40_0.08_0.02.lwmap" }
        if (-not (Test-Path $f)) { Write-Output "MISSING $f"; continue }
        $m = Read-Lwmap $f
        $cols = $m.cols; $rows = $m.rows; $land = $m.land
        $isTri = ($m.tiling -eq 2)

        $rowLand = New-Object 'double[]' $rows
        $rowN = New-Object 'int[]' $rows
        for ($r = 0; $r -lt $rows; $r++) {
            for ($c = 0; $c -lt $cols; $c++) {
                if ($isTri) {
                    $rowLand[$r] += [int]$land[2 * ($r * $cols + $c)] + [int]$land[2 * ($r * $cols + $c) + 1]
                    $rowN[$r] += 2
                } else {
                    $rowLand[$r] += [int]$land[$r * $cols + $c]
                    $rowN[$r] += 1
                }
            }
        }
        $g = 0.0; $tot = 0
        for ($r = 0; $r -lt $rows; $r++) { $g += $rowLand[$r]; $tot += $rowN[$r] }
        $g = $g / [Math]::Max(1, $tot)
        $odd = 0.0; $oddN = 0; $even = 0.0; $evenN = 0
        for ($r = 0; $r -lt $rows; $r++) {
            if (($r % 2) -eq 1) { $odd += $rowLand[$r] / $rowN[$r]; $oddN++ }
            else { $even += $rowLand[$r] / $rowN[$r]; $evenN++ }
        }
        $odd = $odd / [Math]::Max(1, $oddN); $even = $even / [Math]::Max(1, $evenN)
        $rowAlt = [Math]::Abs($odd - $even) / [Math]::Max(1e-9, $g)

        $out = "seed={0} {1} {2}x{3} land={4:P1} rowAlt(odd-even)/g={5:F3}" -f $seed, $tt, $cols, $rows, $g, $rowAlt

        if ($isTri) {
            $mism = 0; $pairs = 0; $upLand = 0.0; $dnLand = 0.0
            for ($r = 0; $r -lt $rows; $r++) {
                for ($c = 0; $c -lt $cols; $c++) {
                    $i0 = 2 * ($r * $cols + $c)
                    $u = [int]$land[$i0]; $d = [int]$land[$i0 + 1]
                    $upLand += $u; $dnLand += $d
                    if ($u -ne $d) { $mism++ }
                    $pairs++
                }
            }
            $out += "  upDnMismatch={0:P1}  up={1:P1} dn={2:P1} upDnGap={3:P1}" -f ($mism / $pairs), ($upLand / $pairs), ($dnLand / $pairs), ([Math]::Abs($upLand - $dnLand) / $pairs)
        }
        $diffs = 0.0; $dn = 0
        for ($r = 0; $r -lt $rows - 1; $r++) {
            $diffs += [Math]::Abs($rowLand[$r] / $rowN[$r] - $rowLand[$r + 1] / $rowN[$r + 1])
            $dn++
        }
        $out += "  avgRowJump={0:F3}" -f ($diffs / [Math]::Max(1, $dn))
        Write-Output $out
    }
}
