#!/usr/bin/env bash
set -euo pipefail

# Build a pinned runtime with bounded virtual reservations.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TASK_CACHE="${KUDROID_FLUTTER_CACHE:-${XDG_CACHE_HOME:-${HOME}/.cache}/kudroid/flutter-runtime}"
SOURCE_DIR="${TASK_CACHE}/flutter"
TOOLS_DIR="${TASK_CACHE}/depot_tools"
SOURCE_REV="fcf2c11572af6f390246c056bc905eca609533a0"
TOOLS_REV="a20039648c0de69d8156906b18052bd47e9bf230"
DART_REV="05589740efb305ceef593b3db6cab2910c17d480"
PATCH_PATH="${SCRIPT_DIR}/dart-constrained-reservations.patch"
MODE="${1:---all}"
JOBS="${KUDROID_FLUTTER_JOBS:-1}"

case "${MODE}" in
    --prepare|--build|--all) ;;
    *) printf 'Usage: %s [--prepare|--build|--all]\n' "$0" >&2; exit 2 ;;
esac
if [[ ! "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    printf 'KUDROID_FLUTTER_JOBS must be a positive integer.\n' >&2
    exit 2
fi
mkdir -p "${TASK_CACHE}"
export PATH="${TOOLS_DIR}:${PATH}"
export DEPOT_TOOLS_UPDATE=0
export PYTHONUNBUFFERED=1

checkout() {
    local repo_url="$1" destination="$2" revision="$3"
    if [[ ! -d "${destination}/.git" ]]; then
        mkdir -p "${destination}"
        timeout 120 git -C "${destination}" init --quiet
        timeout 120 git -C "${destination}" remote add origin "${repo_url}"
        timeout 600 git -C "${destination}" fetch --depth 1 origin "${revision}"
        timeout 120 git -C "${destination}" checkout --detach FETCH_HEAD
    fi
    if [[ "$(git -C "${destination}" rev-parse HEAD)" != "${revision}" ]]; then
        printf 'Unexpected source revision in %s; expected %s.\n' "${destination}" "${revision}" >&2
        exit 1
    fi
}

if [[ "${MODE}" != --build ]]; then
    checkout https://github.com/flutter/flutter.git "${SOURCE_DIR}" "${SOURCE_REV}"
    checkout https://chromium.googlesource.com/chromium/tools/depot_tools.git "${TOOLS_DIR}" "${TOOLS_REV}"
    cat > "${SOURCE_DIR}/.gclient" <<'GCLIENT'
solutions = [{
    "name": ".",
    "url": "https://github.com/flutter/flutter.git",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {
        "download_linux_deps": True,
        "download_fuchsia_deps": False,
        "download_esbuild": False,
        "download_jdk": False,
    },
}]
GCLIENT
    cd "${SOURCE_DIR}"
    timeout 1800 gclient sync --no-history --shallow --nohooks -j 2
    timeout 900 gclient runhooks
fi

if [[ "${MODE}" != --prepare ]]; then
    DART_DIR="${SOURCE_DIR}/engine/src/flutter/third_party/dart"
    if [[ "$(git -C "${SOURCE_DIR}" rev-parse HEAD)" != "${SOURCE_REV}" ||
          "$(git -C "${DART_DIR}" rev-parse HEAD)" != "${DART_REV}" ]]; then
        printf 'Source revisions do not match the pinned runtime.\n' >&2
        exit 1
    fi
    if ! timeout 30 git -C "${DART_DIR}" apply --reverse --check "${PATCH_PATH}" 2>/dev/null; then
        timeout 30 git -C "${DART_DIR}" apply --check "${PATCH_PATH}"
        timeout 30 git -C "${DART_DIR}" apply "${PATCH_PATH}"
    fi
    cd "${SOURCE_DIR}/engine/src"
    timeout 120 ./flutter/tools/gn --android --android-cpu arm64 \
        --runtime-mode release --no-goma --no-rbe --no-stripped --no-enable-unittests --no-lto
    timeout 240 "${SOURCE_DIR}/third_party/ninja/ninja" \
        -C out/android_release_arm64 -j "${JOBS}" libflutter.so
    printf 'Candidate: %s/engine/src/out/android_release_arm64/libflutter.so\n' "${SOURCE_DIR}"
fi
