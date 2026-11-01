#!/usr/bin/env bash
set -euo pipefail

# Build a pinned runtime with bounded virtual reservations.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TASK_CACHE="${KUDROID_FLUTTER_CACHE:-${XDG_CACHE_HOME:-${HOME}/.cache}/kudroid/flutter-runtime}"
TOOLS_DIR="${TASK_CACHE}/depot_tools"
TOOLS_REV="a20039648c0de69d8156906b18052bd47e9bf230"
MODE=--all
VERSION=3.32.5
MATRIX=false
JOBS="${KUDROID_FLUTTER_JOBS:-1}"

while (($#)); do
    case "$1" in
        --prepare|--sources|--tools|--build|--all|--describe) MODE="$1"; shift ;;
        --version) [[ $# -ge 2 ]] || exit 2; VERSION="$2"; shift 2 ;;
        --matrix) MATRIX=true; shift ;;
        *) printf 'Usage: %s [--prepare|--sources|--tools|--build|--all|--describe] [--version TAG|--matrix]\n' "$0" >&2; exit 2 ;;
    esac
done
if ${MATRIX}; then
    for version in 3.24.5 3.22.2 3.16.9 3.10.6 3.32.5; do
        bash "${BASH_SOURCE[0]}" "${MODE}" --version "${version}"
    done
    exit
fi
if [[ ! "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    printf 'KUDROID_FLUTTER_JOBS must be a positive integer.\n' >&2
    exit 2
fi
PIN="$(python3 - "${SCRIPT_DIR}/versions.json" "${VERSION}" <<'PY'
import json, sys
records = json.load(open(sys.argv[1]))
matches = [r for r in records if r['flutter_tag'] == sys.argv[2]]
if len(matches) != 1:
    raise SystemExit('Unsupported Flutter version: ' + sys.argv[2])
print(json.dumps(matches[0]))
PY
)"
if [[ "${MODE}" == --describe ]]; then
    printf '%s\n' "${PIN}"
    exit
fi
read -r SOURCE_REV DART_REV LAYOUT NO_RBE PATCH_RELATIVE DART_RELATIVE < <(python3 - "${PIN}" <<'PY'
import json, sys
r = json.loads(sys.argv[1])
print(r['engine_revision'], r['dart_revision'], r['layout'], int(r['no_rbe']), r['patch'], r['dart_path'])
PY
)
PATCH_PATH="${SCRIPT_DIR}/${PATCH_RELATIVE}"
if [[ "${LAYOUT}" == merged ]]; then
    CLIENT_DIR="${TASK_CACHE}/flutter"
    SOURCE_DIR="${CLIENT_DIR}"
    SRC_DIR="${CLIENT_DIR}/engine/src"
    SOURCE_URL=https://github.com/flutter/flutter.git
    SOLUTION_NAME=.
    NINJA="${CLIENT_DIR}/third_party/ninja/ninja"
else
    CLIENT_DIR="${TASK_CACHE}/versions/${VERSION}"
    SOURCE_DIR="${CLIENT_DIR}/src/flutter"
    SRC_DIR="${CLIENT_DIR}/src"
    SOURCE_URL=https://github.com/flutter/engine.git
    SOLUTION_NAME=src/flutter
    NINJA="${SOURCE_DIR}/third_party/ninja/ninja"
fi
mkdir -p "${TASK_CACHE}"
TASK_CACHE="$(realpath "${TASK_CACHE}")"
export PATH="${TOOLS_DIR}:${PATH}"
export DEPOT_TOOLS_UPDATE=0
export PYTHONUNBUFFERED=1
export CIPD_CACHE_DIR="${TASK_CACHE}/cipd-cache"
export TMPDIR="${TASK_CACHE}/tmp"
mkdir -p "${TMPDIR}" "${CIPD_CACHE_DIR}"

checkout() {
    local repo_url="$1" destination="$2" revision="$3"
    if [[ ! -d "${destination}/.git" ]]; then
        mkdir -p "${destination}"
        timeout 120 git -C "${destination}" init --quiet
        timeout 120 git -C "${destination}" remote add origin "${repo_url}"
    fi
    if ! git -C "${destination}" rev-parse --verify HEAD >/dev/null 2>&1; then
        timeout -k 5 240 git -C "${destination}" fetch --depth 1 origin "${revision}"
        timeout 120 git -C "${destination}" checkout --detach FETCH_HEAD
    fi
    if [[ "$(git -C "${destination}" rev-parse HEAD)" != "${revision}" ]]; then
        printf 'Unexpected source revision in %s; expected %s.\n' "${destination}" "${revision}" >&2
        exit 1
    fi
}

if [[ "${MODE}" != --build ]]; then
    checkout https://chromium.googlesource.com/chromium/tools/depot_tools.git "${TOOLS_DIR}" "${TOOLS_REV}"
    checkout "${SOURCE_URL}" "${SOURCE_DIR}" "${SOURCE_REV}"
    python3 - "${CLIENT_DIR}/.gclient" "${SOLUTION_NAME}" "${SOURCE_URL}" "${PIN}" <<'PY'
import json, sys
solution = dict(name=sys.argv[2], url=sys.argv[3], deps_file='DEPS', managed=False,
                custom_deps=json.loads(sys.argv[4]).get('dependency_mirrors', {}),
                custom_vars=dict(download_linux_deps=True,
                download_fuchsia_deps=False, download_esbuild=False,
                download_jdk=False, use_rbe=False))
with open(sys.argv[1], 'w') as stream:
    stream.write('solutions = ' + repr([solution]) + '\n')
PY
    cd "${CLIENT_DIR}"
    SYNC_OPTIONS=()
    [[ "${MODE}" != --sources ]] || SYNC_OPTIONS+=(--ignore-dep-type cipd)
    [[ "${MODE}" != --tools ]] || SYNC_OPTIONS+=(--ignore-dep-type git)
    timeout -k 5 900 gclient sync --no-history --shallow --nohooks -j 2 \
        --revision "${SOLUTION_NAME}@${SOURCE_REV}" "${SYNC_OPTIONS[@]}"
    if [[ "${MODE}" != --sources ]]; then
        timeout -k 5 600 gclient runhooks
    fi
fi

if [[ "${MODE}" != --prepare && "${MODE}" != --sources && "${MODE}" != --tools ]]; then
    DART_DIR="${CLIENT_DIR}/${DART_RELATIVE}"
    if [[ "$(git -C "${SOURCE_DIR}" rev-parse HEAD)" != "${SOURCE_REV}" ||
          "$(git -C "${DART_DIR}" rev-parse HEAD)" != "${DART_REV}" ]]; then
        printf 'Source revisions do not match the pinned runtime.\n' >&2
        exit 1
    fi
    if ! timeout 30 git -C "${DART_DIR}" apply --reverse --check "${PATCH_PATH}" 2>/dev/null; then
        timeout 30 git -C "${DART_DIR}" apply --check "${PATCH_PATH}"
        timeout 30 git -C "${DART_DIR}" apply "${PATCH_PATH}"
    fi
    cd "${SRC_DIR}"
    GN_OPTIONS=(--android --android-cpu arm64 --runtime-mode release \
        --no-goma --no-stripped --no-enable-unittests --no-lto)
    [[ "${NO_RBE}" == 0 ]] || GN_OPTIONS+=(--no-rbe)
    timeout 120 ./flutter/tools/gn "${GN_OPTIONS[@]}"
    timeout -k 5 240 "${NINJA}" \
        -C out/android_release_arm64 -j "${JOBS}" libflutter.so
    printf 'Candidate (%s): %s/out/android_release_arm64/libflutter.so\n' "${VERSION}" "${SRC_DIR}"
fi
