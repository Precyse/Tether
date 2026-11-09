# warriors-setup: bootstraps the pinned Python (checked by SHA-256), then runs tool\warriors_setup.py with the same arguments.
# usage: warriors-setup.cmd --iso "D:\The Warriors.iso" [--home DIR] [--cache DIR] [--out DIR] [--fullscreen] [--force]
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Option($name) {
    $at = [array]::IndexOf($args, $name)
    if ($at -ge 0 -and $at + 1 -lt $args.Count) { return $args[$at + 1] }
    return $null
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$home_dir = Option '--home'
if (-not $home_dir) { $home_dir = Join-Path $env:LOCALAPPDATA 'WarriorsRecomp' }
$cache = Option '--cache'
if (-not $cache) { $cache = Join-Path $home_dir 'cache' }

$pin = (Get-Content (Join-Path $here 'inputs\pins.json') -Raw | ConvertFrom-Json).python
$target = Join-Path $cache ('tools\python-' + $pin.version)
$python = Join-Path $target 'python.exe'
$mark = Join-Path $target '.unpacked'
if (-not ((Test-Path $mark) -and ((Get-Content $mark -Raw).Trim() -eq $pin.sha256))) {
    Write-Host "[0/9] Fetching Python $($pin.version) (checked by SHA-256)"
    $download = Join-Path $cache ('downloads\python-' + $pin.version)
    New-Item -ItemType Directory -Force $download | Out-Null
    $archive = Join-Path $download $pin.file
    for ($try = 1; $try -le 3; $try++) {
        try { Invoke-WebRequest -UseBasicParsing -Uri $pin.url -OutFile $archive; break }
        catch { if ($try -eq 3) { Write-Host "ERROR: could not download $($pin.url): $($_.Exception.Message). Check the internet connection and run again."; exit 1 } }
    }
    if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLower() -ne $pin.sha256) {
        Remove-Item $archive
        Write-Host "ERROR: the downloaded Python does not match its expected SHA-256 and was deleted. Run again; if it repeats, the download is being altered."
        exit 1
    }
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Expand-Archive -Path $archive -DestinationPath $target
    Remove-Item $archive
    Set-Content -Path $mark -Value $pin.sha256 -NoNewline
}
& $python -I (Join-Path $here 'tool\warriors_setup.py') @args
exit $LASTEXITCODE
