[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$AnalysisDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvDir = Join-Path $AnalysisDir ".venv-analysis"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"

function Find-Uv {
    # NOTE: every external command in here must pipe to Out-Host — in PowerShell a
    # function's return value is its whole pipeline output, so a bare `winget install`
    # or `uv --version` pollutes the returned path with banner text and the caller
    # then tries to execute that text as a command.
    $UvCommand = Get-Command uv -ErrorAction SilentlyContinue
    if ($null -ne $UvCommand) {
        & $UvCommand.Source --version | Out-Host
        return $UvCommand.Source
    }
    Write-Host "uv was not found; trying winget install astral-sh.uv"
    $Winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($null -ne $Winget) {
        & $Winget.Source install --id astral-sh.uv --exact --accept-package-agreements --accept-source-agreements | Out-Host
    }
    # A fresh winget install lands in WinGet\Links, which this session's PATH may not
    # include yet — probe the known location before declaring failure.
    $UvCommand = Get-Command uv -ErrorAction SilentlyContinue
    if ($null -ne $UvCommand) { return $UvCommand.Source }
    $WingetLink = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links\uv.exe"
    if (Test-Path -LiteralPath $WingetLink) { return $WingetLink }
    Write-Host "winget did not make uv available; trying python -m pip install --user uv"
    & python -m pip install --user uv | Out-Host
    $UvCommand = Get-Command uv -ErrorAction SilentlyContinue
    if ($null -ne $UvCommand) { return $UvCommand.Source }
    $UserBase = (& python -m site --user-base).Trim()
    $Candidate = Join-Path $UserBase "Scripts\uv.exe"
    if (Test-Path -LiteralPath $Candidate) { return $Candidate }
    throw "uv installation finished but uv.exe could not be found. Open a new terminal and rerun setup.ps1."
}

$Uv = Find-Uv
Write-Host "Creating isolated analysis environment at $VenvDir"
& $Uv venv $VenvDir

$HasNvidia = $false
$NvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if ($null -ne $NvidiaSmi) {
    & $NvidiaSmi.Source | Out-Null
    $HasNvidia = ($LASTEXITCODE -eq 0)
}

if ($HasNvidia) {
    Write-Host "NVIDIA GPU detected; installing CUDA 12.8 torch wheels"
    & $Uv pip install --python $VenvPython torch torchaudio --index-url https://download.pytorch.org/whl/cu128
    if ($LASTEXITCODE -eq 0) {
        & $VenvPython -c "import torch, sys; sys.exit(0 if torch.cuda.is_available() else 1)"
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "CUDA torch install failed; recreating the environment and falling back to CPU"
        Remove-Item -LiteralPath $VenvDir -Recurse -Force
        & $Uv venv $VenvDir
        & $Uv pip install --python $VenvPython torch torchaudio --index-url https://download.pytorch.org/whl/cpu
    }
} else {
    Write-Host "No working NVIDIA GPU detected; installing CPU torch wheels"
    & $Uv pip install --python $VenvPython torch torchaudio --index-url https://download.pytorch.org/whl/cpu
}
if ($LASTEXITCODE -ne 0) { throw "torch installation failed" }

& $Uv pip install --python $VenvPython -r (Join-Path $AnalysisDir "requirements-analysis.txt")
if ($LASTEXITCODE -ne 0) { throw "analysis dependency installation failed" }

Write-Host "Setup complete. Next run:"
Write-Host ".\analyze.ps1 -Root \"C:\path\to\music\""
