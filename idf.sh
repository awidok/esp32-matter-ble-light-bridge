#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd)"
idf_path="$project_dir/.tooling/esp-idf"
tools_path="$project_dir/.tooling/espressif-tools"
matter_path="$project_dir/.tooling/esp-matter"

if [[ ! -f "$idf_path/export.sh" ]]; then
    echo "Local ESP-IDF is missing at: $idf_path" >&2
    echo "Install ESP-IDF 5.x there or activate another ESP-IDF environment." >&2
    exit 1
fi

export IDF_TOOLS_PATH="$tools_path"
source "$idf_path/export.sh" >/dev/null

export ESP_MATTER_PATH="$matter_path"
if [[ -d "$matter_path/connectedhomeip/connectedhomeip/.environment" ]]; then
    source "$matter_path/export.sh" >/dev/null
fi

exec idf.py "$@"
