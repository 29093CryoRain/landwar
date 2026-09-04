# check_stripes.ps1 — P12 抗锯齿重采样验证：解析 native landmap，检查
#   ① 逐行海陆占比的行交替相关性（"横纹"信号）；
#   ② 三角正/反格海陆不一致率 + 正/反各自海陆占比差（"同朝向区"信号）。
# 注意：变量名大小写不敏感——勿用 $B/$G/$R 覆盖字节数组。
# 用法: powershell -File tools/check_stripes.ps1 [-Seeds 42,43,44] [-Fc]   （-Fc = 探针 forceCoast 图）
param([int[]]$Seeds = @(42, 43, 44), [switch]$Fc)

function Read-Landmap([string]$Path) {
    $root = (Get-Content -LiteralPath $Path -Raw) | ConvertFrom-Json
    $tiling = $root.tiling
    $cols = [int]$root.cols; $rows = [int]$root.rows
    $base = 1; if ($tiling -eq 'tri') { $base = 2 }
    $land = [bool[]]::new($cols * $rows * $base)
    for ($r = 0; $r -lt $rows; $r++) {
        $encoded = [string]$root.terrain[$r]
        for ($i = 0; $i -lt $encoded.Length; $i++) { $land[$r * $cols * $base + $i] = $encoded[$i] -ne 'S' }
    }
    return @{ tiling = $tiling; cols = $cols; rows = $rows; land = $land }
}

foreach ($seed in $Seeds) {
    foreach ($tt in @('hex', 'tri')) {
        $pattern = if ($Fc) { "probe_${seed}_${tt}_fc.landmap" } else { "gen_${seed}_${tt}_*.landmap" }
        $match = Get-ChildItem -LiteralPath "userdata\maps" -Filter $pattern -File -ErrorAction SilentlyContinue
        $f = if ($match) { $match[0].FullName } else { "userdata\maps\$pattern" }
        if (-not (Test-Path $f)) { Write-Output "MISSING $f"; continue }
        $m = Read-Landmap $f
        $cols = $m.cols; $rows = $m.rows; $land = $m.land
        $isTri = ($m.tiling -eq 'tri')

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
