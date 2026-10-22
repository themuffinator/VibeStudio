param(
    [ValidateSet('switchback','spillway','counterweight','talus','undercut','fold','lockyard','ravel')][string]$Recipe = 'switchback',
    [string]$ToolRoot = 'E:\_SOURCE\_CODE\ericw-tools-2.0.0-alpha11',
    [string]$Assets = 'E:\_SOURCE\_ASSETS\Q2'
)
$ErrorActionPreference = 'Stop'
$recipeProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$recipeOutput = [IO.Path]::GetFullPath((Join-Path $recipeProject ('out\' + $Recipe)))
$recipeMap = 'qmg_' + $Recipe
$recipeTarget = $recipeMap + '_recipe'
foreach ($recipePath in @($recipeProject, (Join-Path $recipeProject 'out'), $recipeOutput)) {
    if ((Test-Path -LiteralPath $recipePath) -and
        ((Get-Item -LiteralPath $recipePath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Recipe output may not traverse a link.'
    }
}
New-Item -ItemType Directory -Force -Path $recipeOutput | Out-Null
$recipePins = @{
    'qbsp.exe' = 'fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8'
    'vis.exe' = '01be572845e05c87b01a0b667e590ac49bdaba23369af8529fe82fcfbc4fe6cf'
    'light.exe' = '8e4fe9821f73c2d5c8a13655be20d3a71f357e76171de37c02413f4da3131a2f'
}
foreach ($recipeTool in $recipePins.Keys) {
    if ((Get-FileHash -LiteralPath (Join-Path $ToolRoot $recipeTool) -Algorithm SHA256).Hash -ne $recipePins[$recipeTool]) {
        throw "Tool hash mismatch: $recipeTool"
    }
}
function Run-RecipeStage([string]$Executable, [string[]]$Arguments, [string]$Log) {
    $recipeStagePreference = $ErrorActionPreference
    try {
        # Windows PowerShell must retain native stderr before testing exit status.
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Out-File -LiteralPath (Join-Path $recipeOutput $Log) -Encoding utf8
        $recipeStageExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $recipeStagePreference }
    if ($recipeStageExit -ne 0) { throw "Recipe stage failed: $Executable; see $Log" }
}
Push-Location -LiteralPath $recipeProject
try {
    Run-RecipeStage 'cmake' @('--build','--preset','windows-clang','--target',$recipeTarget) 'recipe-build.log'
    Run-RecipeStage (Join-Path $recipeProject ('build\windows-clang\' + $recipeTarget + '.exe')) @($recipeOutput) 'generation.log'
    $recipeBsp = Join-Path $recipeOutput ($recipeMap + '.bsp')
    Run-RecipeStage (Join-Path $ToolRoot 'qbsp.exe') @('-q2bsp','-leaktest','-noallowupgrade',
        '-nodefaultpaths','-path',$Assets,'-lowpriority','0','-nolog','-nocolor','-quiet',
        (Join-Path $recipeOutput ($recipeMap + '.map')),$recipeBsp) 'qbsp.log'
    Run-RecipeStage (Join-Path $ToolRoot 'vis.exe') @('-threads','2','-lowpriority','0','-nostate',
        '-nodefaultpaths','-path',$Assets,'-nolog','-nocolor','-quiet',$recipeBsp) 'vis.log'
    Run-RecipeStage (Join-Path $ToolRoot 'light.exe') @('-extra4','-bounce','1','-soft','-threads','2',
        '-lowpriority','0','-nodefaultpaths','-path',$Assets,'-nolog','-nocolor','-quiet',$recipeBsp) 'light.log'
    Run-RecipeStage 'python' @('-B','-m','tools.map_study','compare','--bsp',$recipeBsp,'--cohort','dm_small',
        '--profile',(Join-Path $recipeProject 'out\map-study\profile.json'),
        '--output',(Join-Path $recipeOutput 'reference-comparison.json')) 'comparison.log'
    Run-RecipeStage 'python' @('-B','-m','tools.map_recipes.inspect','--recipe',$Recipe,'--output',$recipeOutput,'--assets',$Assets) 'verification.log'
    if ($Recipe -ne 'switchback') {
        Run-RecipeStage 'python' @('-B','-m','tools.map_recipes.render_layout','--output',$recipeOutput) 'preview-export.log'
    }
    Write-Output "$Recipe compiled and packaged: $recipeOutput"
} finally { Pop-Location }
