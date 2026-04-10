#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$(dirname "$PROJECT_DIR")")"
HTM_CORE_WHEEL="$REPO_ROOT/htm.core/dist/htm-2.2.0-cp313-cp313-macosx_15_0_arm64.whl"
VENV_DIR="$PROJECT_DIR/venv"

echo "==> Creating venv at $VENV_DIR"
python3.13 -m venv "$VENV_DIR"

echo "==> Upgrading pip"
"$VENV_DIR/bin/pip" install --upgrade pip

# setuptools>=71 drops pkg_resources which htm.core depends on
echo "==> Pinning setuptools for pkg_resources compatibility"
"$VENV_DIR/bin/pip" install "setuptools<71"

echo "==> Installing requirements (py313 compatible)"
"$VENV_DIR/bin/pip" install -r "$PROJECT_DIR/requirements-py313.txt"

echo "==> Installing htm.core from local wheel"
"$VENV_DIR/bin/pip" install "$HTM_CORE_WHEEL"

echo "==> Installing htm_source package (editable, no-deps)"
"$VENV_DIR/bin/pip" install --no-deps -e "$PROJECT_DIR"

echo ""
echo "Setup complete. Run with: ./scripts/run_swat.sh"
