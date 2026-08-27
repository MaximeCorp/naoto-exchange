#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for step in "${SCRIPT_DIR}"/[0-9][0-9]-*.sh; do
    echo "=== Running $(basename "${step}") ==="
    "${step}"
done

echo "=== Bootstrap complete ==="