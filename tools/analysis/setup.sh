#!/usr/bin/env sh
set -eu

analysis_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
venv_dir="$analysis_dir/.venv-analysis"

if ! command -v uv >/dev/null 2>&1; then
    echo "uv was not found; installing with python -m pip install --user uv"
    python3 -m pip install --user uv
    export PATH="$(python3 -m site --user-base)/bin:$PATH"
fi
uv --version
uv venv "$venv_dir"

if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi >/dev/null 2>&1; then
    echo "NVIDIA GPU detected; installing CUDA 12.8 torch wheels"
    if ! uv pip install --python "$venv_dir/bin/python" torch torchaudio --index-url https://download.pytorch.org/whl/cu128; then
        echo "CUDA torch install failed; recreating the environment and falling back to CPU"
        rm -rf -- "$venv_dir"
        uv venv "$venv_dir"
        uv pip install --python "$venv_dir/bin/python" torch torchaudio --index-url https://download.pytorch.org/whl/cpu
    fi
else
    echo "No working NVIDIA GPU detected; installing CPU torch wheels"
    uv pip install --python "$venv_dir/bin/python" torch torchaudio --index-url https://download.pytorch.org/whl/cpu
fi
uv pip install --python "$venv_dir/bin/python" -r "$analysis_dir/requirements-analysis.txt"
echo "Setup complete. Next run:"
echo "$venv_dir/bin/python $analysis_dir/analyze_all.py --root /path/to/music"
