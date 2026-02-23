# Generate lua.lib from lua.dll using MSVC dumpbin + lib.exe
# Called from Dockerfile.windows after COPY . C:\src\
# Locates the MSVC toolchain version dynamically so it works across VS updates.

$ErrorActionPreference = 'Stop'

$msvcRoot = 'C:\BuildTools\VC\Tools\MSVC'
$version  = (Get-ChildItem $msvcRoot | Sort-Object Name -Descending | Select-Object -First 1).Name
$binDir   = "$msvcRoot\$version\bin\Hostx64\x64"
$dumpbin  = "$binDir\dumpbin.exe"
$lib      = "$binDir\lib.exe"

Write-Host "Using MSVC $version"
Write-Host "dumpbin: $dumpbin"
Write-Host "lib:     $lib"

Set-Location C:\src\lib

$raw   = & $dumpbin /exports lua.dll
$names = $raw | Where-Object { $_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)' } |
                ForEach-Object { $Matches[1] }

if (-not $names) { throw 'No exports found in lua.dll — dumpbin output was empty or unparseable' }

@('LIBRARY lua', 'EXPORTS') + $names | Set-Content -Encoding ASCII lua.def
Write-Host "lua.def written with $($names.Count) exports"

& $lib /def:lua.def /out:lua.lib /machine:x64
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed with exit code $LASTEXITCODE" }

Write-Host 'lua.lib generated successfully'
