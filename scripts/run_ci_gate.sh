#!/bin/zsh
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
firmware_root="$repo_root/firmware"
idf_root="${PONCHAN_ESP_IDF_PATH:-$repo_root/../.toolchains/esp-idf-v5.5.4}"
temp_root="${TMPDIR:-/tmp}"
temp_root="${temp_root%/}"
ci_tmp="$(mktemp -d "$temp_root/stackchan-firmware-ci.XXXXXX")"

cleanup() {
  case "$ci_tmp" in
    "$temp_root"/stackchan-firmware-ci.*) rm -rf -- "$ci_tmp" ;;
    *) print -u2 "Unexpected CI temp path: $ci_tmp"; return 1 ;;
  esac
}
trap cleanup EXIT

if [[ ! -f "$idf_root/export.sh" ]]; then
  print -u2 "ESP-IDF v5.5.4 is required at: $idf_root"
  exit 1
fi

source "$idf_root/export.sh" >/dev/null
idf_version="$(idf.py --version)"
if [[ "$idf_version" != "ESP-IDF v5.5.4" ]]; then
  print -u2 "Unexpected ESP-IDF version: $idf_version"
  exit 1
fi

git -C "$repo_root" diff --check
fetch_arguments=()
if [[ "${STACKCHAN_CI_CLEAN_DEPENDENCIES:-0}" == "1" ]]; then
  fetch_arguments+=(--clean)
fi
python3 "$firmware_root/fetch_repos.py" "${fetch_arguments[@]}"

cmake -S "$firmware_root/tests" -B "$ci_tmp/host-tests"
cmake --build "$ci_tmp/host-tests" --parallel
ctest --test-dir "$ci_tmp/host-tests" --output-on-failure

ccache_enabled=0
if command -v ccache >/dev/null 2>&1; then
  ccache_enabled=1
fi

STACKCHAN_SDKCONFIG="$ci_tmp/sdkconfig" \
STACKCHAN_SDKCONFIG_DEFAULTS="$firmware_root/sdkconfig.defaults" \
IDF_CCACHE_ENABLE="$ccache_enabled" \
  idf.py -C "$firmware_root" -B "$ci_tmp/build" \
    -D "SDKCONFIG=$ci_tmp/sdkconfig" build

git -C "$repo_root" diff --exit-code
