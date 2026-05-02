param(
  [string]$BuildDir = "builddir",
  [string]$Backend = "ninja"
)

$ErrorActionPreference = "Stop"

function Find-QMake {
  if ($env:QMAKE -and (Test-Path $env:QMAKE)) {
    return $env:QMAKE
  }

  $cmd = Get-Command qmake6 -ErrorAction SilentlyContinue
  if ($cmd) {
    return $cmd.Source
  }

  $roots = @($env:Qt6_DIR, $env:QTDIR, $env:QT_DIR, "C:\\Qt") | Where-Object { $_ -and (Test-Path $_) }
  foreach ($root in $roots) {
    $matches = Get-ChildItem -Path $root -Filter qmake6.exe -File -Recurse -Depth 6 -ErrorAction SilentlyContinue
    if ($matches) {
      $preferred = $matches | Where-Object { $_.FullName -match 'msvc|clang' } | Select-Object -First 1
      if ($preferred) {
        return $preferred.FullName
      }
      return $matches[0].FullName
    }
  }

  return $null
}

$qmake = Find-QMake
if ($qmake) {
  $env:QMAKE = $qmake
  $qtBin = Split-Path -Parent $qmake
  $env:PATH = "$qtBin;$env:PATH"
  Write-Host "Using qmake: $qmake"
} else {
  Write-Host "qmake6 not found. Install Qt6 or set QMAKE/QT_DIR/Qt6_DIR." -ForegroundColor Yellow
  exit 1
}

if (!(Test-Path "$BuildDir/meson-info") -or !(Test-Path "$BuildDir/build.ninja")) {
  if (Test-Path $BuildDir) {
    meson setup $BuildDir --backend $Backend --wipe
  } else {
    meson setup $BuildDir --backend $Backend
  }
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} else {
  meson setup $BuildDir --reconfigure
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

meson compile -C $BuildDir
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

meson test -C $BuildDir --print-errorlogs
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$BinaryPath = Join-Path $BuildDir "src/vibestudio"
if ($IsWindows -or $env:OS -eq "Windows_NT") {
  $BinaryPath = Join-Path $BuildDir "src/vibestudio.exe"
}

python scripts/validate_build.py --binary "$BinaryPath" --expected-version "$((Get-Content VERSION -Raw).Trim())"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

python scripts/validate_samples.py --binary "$BinaryPath"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

python scripts/validate_packaging.py --binary "$BinaryPath"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

python scripts/validate_release_assets.py --binary "$BinaryPath"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
