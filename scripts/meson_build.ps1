param(
  [string]$BuildDir = "builddir",
  [string]$Backend = "ninja",
  [int]$WarningLevel = -1,
  [switch]$WarningsAsErrors,
  [switch]$ConfigureOnly,
  [switch]$SkipTests,
  [switch]$SkipValidation
)

$ErrorActionPreference = "Stop"

function Is-WindowsHost {
  return $IsWindows -or $env:OS -eq "Windows_NT"
}

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

function Use-CompatibleWindowsCompiler {
  param(
    [string]$QMakePath
  )

  if (!(Is-WindowsHost)) {
    return
  }

  if ($env:CXX) {
    Write-Host "Using C++ compiler from CXX: $env:CXX"
    return
  }

  if ($QMakePath -notmatch 'msvc|clang') {
    return
  }

  $clangCl = Get-Command clang-cl -ErrorAction SilentlyContinue
  if ($clangCl) {
    $env:CC = $clangCl.Source
    $env:CXX = $clangCl.Source
    Write-Host "Using C++ compiler: $($clangCl.Source)"
  }
}

function BuildDirUsesCompiler {
  param(
    [string]$BuildDir,
    [string]$CompilerName
  )

  if (!(Test-Path (Join-Path $BuildDir "meson-info"))) {
    return $true
  }

  try {
    $compilerInfo = meson introspect $BuildDir --compilers 2>$null | ConvertFrom-Json
    $compilerExe = $compilerInfo.host.cpp.exelist | Select-Object -First 1
    if (!$compilerExe) {
      return $false
    }
    return [System.IO.Path]::GetFileNameWithoutExtension($compilerExe) -eq $CompilerName
  } catch {
    return $false
  }
}

$qmake = Find-QMake
if ($qmake) {
  $env:QMAKE = $qmake
  $qtBin = Split-Path -Parent $qmake
  $env:PATH = "$qtBin;$env:PATH"
  Write-Host "Using qmake: $qmake"
  Use-CompatibleWindowsCompiler -QMakePath $qmake
} else {
  Write-Host "qmake6 not found. Install Qt6 or set QMAKE/QT_DIR/Qt6_DIR." -ForegroundColor Yellow
  exit 1
}

$needsCompilerWipe = $false
if ((Is-WindowsHost) -and $env:CXX -and ([System.IO.Path]::GetFileNameWithoutExtension($env:CXX) -eq "clang-cl")) {
  $needsCompilerWipe = !(BuildDirUsesCompiler -BuildDir $BuildDir -CompilerName "clang-cl")
  if ($needsCompilerWipe) {
    Write-Host "Existing build directory was configured with an incompatible compiler; wiping for clang-cl/MSVC Qt compatibility." -ForegroundColor Yellow
  }
}

$setupOptions = @()
if ($WarningLevel -ge 0) {
  $setupOptions += "-Dwarning_level=$WarningLevel"
}
if ($WarningsAsErrors) {
  $setupOptions += "-Dwerror=true"
}

if (!(Test-Path "$BuildDir/meson-info") -or !(Test-Path "$BuildDir/build.ninja") -or $needsCompilerWipe) {
  if (Test-Path $BuildDir) {
    meson setup $BuildDir --backend $Backend --wipe @setupOptions
  } else {
    meson setup $BuildDir --backend $Backend @setupOptions
  }
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} else {
  meson setup $BuildDir --reconfigure @setupOptions
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

if ($ConfigureOnly) {
  exit 0
}

meson compile -C $BuildDir
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

if (!$SkipTests) {
  meson test -C $BuildDir --print-errorlogs
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

if ($SkipValidation) {
  exit 0
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
