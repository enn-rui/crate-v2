@echo off
setlocal
set "ANALYSIS_DIR=%~dp0"
set "ANALYSIS_PYTHON=%ANALYSIS_DIR%.venv-analysis\Scripts\python.exe"
if not exist "%ANALYSIS_PYTHON%" (
  echo Analysis environment not found. Run setup.ps1 first. 1>&2
  exit /b 1
)
"%ANALYSIS_PYTHON%" "%ANALYSIS_DIR%analyze_all.py" %*
exit /b %ERRORLEVEL%
