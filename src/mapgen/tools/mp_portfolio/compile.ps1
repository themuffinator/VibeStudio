param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-z][a-z0-9_-]*$')][string]$MapName,
    [switch]$FullLighting,
    [string]$ToolRoot = 'E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11',
    [string]$Assets = 'E:/_SOURCE/_ASSETS/Q2'
)
$ErrorActionPreference = 'Stop'
$portfolioProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$portfolioOutput = (Resolve-Path -LiteralPath $Output).Path
if (-not $portfolioOutput.StartsWith($portfolioProject + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Portfolio outputs must remain inside the project.'
}
$portfolioParent = Get-Item -LiteralPath $portfolioOutput -Force
while ($null -ne $portfolioParent) {
    if ($portfolioParent.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked output ancestor.' }
    $portfolioParent = $portfolioParent.Parent
}
$portfolioPins = @{
    'qbsp.exe' = 'fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8'
    'vis.exe' = '01be572845e05c87b01a0b667e590ac49bdaba23369af8529fe82fcfbc4fe6cf'
    'light.exe' = '8e4fe9821f73c2d5c8a13655be20d3a71f357e76171de37c02413f4da3131a2f'
}
foreach ($portfolioTool in $portfolioPins.Keys) {
    if ((Get-FileHash -LiteralPath (Join-Path $ToolRoot $portfolioTool) -Algorithm SHA256).Hash -ne $portfolioPins[$portfolioTool]) {
        throw "Compiler identity changed: $portfolioTool"
    }
}
foreach ($portfolioLeaf in Get-ChildItem -LiteralPath $portfolioOutput -Force) {
    if ($portfolioLeaf.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked output entry.' }
}
function Invoke-PortfolioStage([string]$Executable, [string[]]$Arguments, [string]$Log) {
    & $Executable @Arguments 2>&1 | Out-File -LiteralPath (Join-Path $portfolioOutput $Log) -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "Compiler failed: $Executable; see $Log" }
}
Push-Location -LiteralPath $portfolioProject
try {
    $portfolioBsp = Join-Path $portfolioOutput ($MapName + '.bsp')
    Invoke-PortfolioStage (Join-Path $ToolRoot 'qbsp.exe') @('-q2bsp','-leaktest','-noallowupgrade',
        '-nodefaultpaths','-path',$Assets,'-lowpriority','0','-nolog','-nocolor','-quiet',
        (Join-Path $portfolioOutput ($MapName + '.map')),$portfolioBsp) ($MapName + '-qbsp.log')
    if ($FullLighting) {
        Invoke-PortfolioStage (Join-Path $ToolRoot 'vis.exe') @('-threads','2','-lowpriority','0','-nostate',
            '-nodefaultpaths','-path',$Assets,'-nolog','-nocolor','-quiet',$portfolioBsp) ($MapName + '-vis.log')
        Invoke-PortfolioStage (Join-Path $ToolRoot 'light.exe') @('-extra4','-bounce','1','-soft','-threads','2',
            '-lowpriority','0','-nodefaultpaths','-path',$Assets,'-nolog','-nocolor','-quiet',$portfolioBsp) ($MapName + '-light.log')
    }
    Write-Output "Compiled $MapName (full lighting: $FullLighting)"
} finally { Pop-Location }
