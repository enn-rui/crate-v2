[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Root,
    [string]$Out,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArgs
)

$ErrorActionPreference = "Stop"
$AnalysisDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Python = Join-Path $AnalysisDir ".venv-analysis\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $Python)) {
    Write-Error "Analysis environment not found. Run setup.ps1 first."
    exit 1
}
$Arguments = @((Join-Path $AnalysisDir "analyze_all.py"))
if ($Root) { $Arguments += @("--root", $Root) }
if ($Out) { $Arguments += @("--out", $Out) }
if ($RemainingArgs) { $Arguments += $RemainingArgs }
& $Python @Arguments
exit $LASTEXITCODE
