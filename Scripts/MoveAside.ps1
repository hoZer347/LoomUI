# Move-LockedAside: renames an exe that cannot be opened for writing, such as a running one, to
# <name> <guid>.replaced beside it, so build.ps1 can link over its path. Remove-Replaced deletes
# the ones no longer running. Dot-source it.

function Remove-Replaced
{
    param(
        [Parameter(Mandatory)]
        [string] $Folder
    )

    Get-ChildItem $Folder -Filter '*.replaced' -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue
}

function Move-LockedAside
{
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return $false
    }

    try
    {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $stream.Dispose()

        return $false
    }
    catch [IO.IOException]
    {
    }

    $folder = Split-Path $Path -Parent
    $name = [IO.Path]::GetFileNameWithoutExtension($Path)
    Move-Item -ErrorAction Stop -LiteralPath $Path (Join-Path $folder "$name $([guid]::NewGuid()).replaced")

    return $true
}
