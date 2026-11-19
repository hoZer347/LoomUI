# Enter-Toolchain: puts the Visual Studio developer environment and, for clang, clang-cl on this
# shell's PATH, and returns the compiler the build will use ('clang' or 'msvc'). Dot-source it.
# clang-cl comes from -LlvmFolder (an LLVM folder or its bin), then Visual Studio's own
# LLVM, then PATH, then the newest %LOCALAPPDATA%\llvm-*\bin. Without one, it warns and returns 'msvc'.
# Get-BuildJobs: the parallel compile jobs, 80% of the logical processors and at least one.

$script:VisualStudioInstaller = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'

function Find-VisualStudio
{
    $vswhere = Join-Path $script:VisualStudioInstaller 'vswhere.exe'
    if (Test-Path $vswhere)
    {
        $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath | Select-Object -First 1
        if ($found) { return $found }
    }

    $instance = Get-CimInstance MSFT_VSInstance -Namespace root/cimv2/vs -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($instance) { return $instance.InstallLocation }

    throw 'Could not locate Visual Studio. Install it with the C++ build tools.'
}

function Find-ClangFolder([string] $visualStudio, [string] $llvmFolder)
{
    $candidates = @()

    if ($llvmFolder)
    {
        $candidates += $llvmFolder
        $candidates += Join-Path $llvmFolder 'bin'
    }

    $candidates += Join-Path $visualStudio 'VC\Tools\Llvm\x64\bin'

    $onPath = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += Split-Path $onPath.Source }

    $candidates += @(Get-ChildItem (Join-Path $env:LOCALAPPDATA 'llvm-*') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^llvm-(\d+(\.\d+)*)$' } |
        Sort-Object { [version] "$($_.Name.Substring('llvm-'.Length)).0" } -Descending |
        ForEach-Object { Join-Path $_.FullName 'bin' })

    foreach ($candidate in $candidates)
    {
        if ((Test-Path (Join-Path $candidate 'clang-cl.exe')) -and (Test-Path (Join-Path $candidate 'lld-link.exe')))
        {
            return $candidate
        }
    }

    return $null
}

$cpuShare = 0.8

function Get-BuildJobs([int] $Processors = [Environment]::ProcessorCount)
{
    return [Math]::Max(1, [int][Math]::Floor($Processors * $cpuShare))
}

function Add-ToPath([string] $folder, [string] $program)
{
    if (-not (Get-Command $program -ErrorAction SilentlyContinue) -and (Test-Path $folder))
    {
        $env:PATH = "$folder;$env:PATH"
    }
}

function Enter-Toolchain([ValidateSet('clang', 'msvc')] [string] $Compiler = 'clang', [string] $LlvmFolder = '')
{
    $visualStudio = Find-VisualStudio

    Add-ToPath $script:VisualStudioInstaller 'vswhere.exe'
    Import-Module (Join-Path $visualStudio 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    $global:ErrorActionPreference = 'Stop'

    $cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake'
    Add-ToPath (Join-Path $cmake 'CMake\bin') 'cmake.exe'
    Add-ToPath (Join-Path $cmake 'Ninja') 'ninja.exe'

    foreach ($program in 'cmake.exe', 'ninja.exe')
    {
        if (-not (Get-Command $program -ErrorAction SilentlyContinue))
        {
            throw "Could not find $program. Install the Visual Studio CMake component or put it on PATH."
        }
    }

    if ($Compiler -eq 'msvc') { return 'msvc' }

    $clang = Find-ClangFolder $visualStudio $LlvmFolder
    if (-not $clang)
    {
        Write-Warning 'clang-cl was not found (-LlvmFolder, Visual Studio LLVM, PATH, %LOCALAPPDATA%\llvm-*); building with MSVC.'
        return 'msvc'
    }

    $env:PATH = "$clang;$env:PATH"
    return 'clang'
}
