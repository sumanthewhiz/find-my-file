param(
    [ValidateSet('x64','ARM64','Win32')][string]$Platform = 'x64',
    [string]$Toolset = '',
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if (!$NoBuild) {
        $command = Get-Command msbuild -ErrorAction SilentlyContinue
        $msbuild = if ($command) { $command.Source } else { $null }
        if (!$msbuild) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (!(Test-Path $vswhere)) { throw 'Visual Studio C++ build tools are required.' }
            $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        }
        if (!$msbuild) { throw 'MSBuild was not found.' }
        if (!$Toolset) {
            $Toolset = if ((Get-Item $msbuild).VersionInfo.FileMajorPart -ge 18) { 'v145' } else { 'v143' }
        }
        $flags = @('/p:Configuration=Release', "/p:Platform=$Platform", '/m', '/nologo', '/v:minimal')
        if ($Toolset) { $flags += "/p:PlatformToolset=$Toolset" }
        & $msbuild 'tests\WarpContextTests.vcxproj' @flags
        if ($LASTEXITCODE) { throw "Native build failed ($LASTEXITCODE)." }
        & $msbuild 'tests\SearchPipelineTests.vcxproj' @flags
        if ($LASTEXITCODE) { throw "Search pipeline build failed ($LASTEXITCODE)." }
    }
    & "build\tests\$Platform\SearchPipelineTests.exe"
    if ($LASTEXITCODE) { throw 'Real search pipeline regressions failed.' }
    & python -m unittest discover -s tests -p 'test_*.py'
    if ($LASTEXITCODE) { throw 'Metric unit tests failed.' }
    & python 'tests\evaluate_context.py' --native-bin "build\tests\$Platform\WarpContextTests.exe" --output-dir "build\context-eval-$Platform"
    if ($LASTEXITCODE) { throw 'Consumer evaluation failed; see build\context-eval-*.' }
} finally {
    Pop-Location
}
