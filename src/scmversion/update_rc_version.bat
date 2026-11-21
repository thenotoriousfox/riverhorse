@echo off

FOR /F "tokens=* USEBACKQ" %%g IN (`git describe --tags --exclude latest --dirty`) do (SET "TAG=%%g")

powershell -NoProfile -Command "$m = [regex]::Match('%TAG%', '^v?(\d+)\.(\d+)(?:\.(\d+))?'); if ($m.Success) { $v = @($m.Groups[1].Value, $m.Groups[2].Value, [int]('0' + $m.Groups[3].Value), 0); (gc ..\riverhorse-qt\riverhorse-qt.rc) -replace '1,0,0,1', ($v -join ',') -replace '1\.0\.0\.1', ($v -join '.') | Out-File -encoding ASCII ..\riverhorse-qt\riverhorse-qt.rc }"
