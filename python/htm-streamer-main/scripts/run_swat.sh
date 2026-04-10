#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VENV_DIR="$PROJECT_DIR/venv"

if [ ! -f "$VENV_DIR/bin/activate" ]; then
    echo "ERROR: venv not found. Run ./scripts/setup_venv.sh first."
    exit 1
fi

source "$VENV_DIR/bin/activate"
cd "$PROJECT_DIR"

echo "==> Running quickstart-swat.py"
python quickstart-swat.py
