#!/usr/bin/env bash

set -euo pipefail

function add_ssh_key() {
  local key_string="${1:-}"
  local key_name="${2:-id_rsa}"
  if [[ -z "${key_string}" ]]; then
    echo "SSH key for ${key_name} is empty; skip key setup."
    return
  fi

  mkdir -p ~/.ssh
  chmod 700 ~/.ssh
  printf "%s" "${key_string}" > "${HOME}/.ssh/${key_name}_base64"
  base64 --decode --ignore-garbage "${HOME}/.ssh/${key_name}_base64" > "${HOME}/.ssh/${key_name}"
  chmod 600 "${HOME}/.ssh/${key_name}"
}

function add_gitlab_ssh_keys() {
  add_ssh_key "${GITLAB_KEY:-}" "id_rsa"

  mkdir -p ~/.ssh
  chmod 700 ~/.ssh
  {
    printf "Host gitlab.espressif.cn\n\tStrictHostKeyChecking no\n"
    if [[ -n "${LOCAL_GITLAB_SSH_SERVER:-}" ]]; then
      local srv="${LOCAL_GITLAB_SSH_SERVER##*@}"
      srv="${srv%%:*}"
      printf "Host %s\n\tStrictHostKeyChecking no\n" "${srv}"
    fi
  } >> ~/.ssh/config
}

function add_github_ssh_keys() {
  add_ssh_key "${GH_PUSH_KEY:-}" "id_rsa"
  echo -e "Host github.com\n\tStrictHostKeyChecking no\n" >> ~/.ssh/config
}

function add_github_https_credentials() {
  local github_user="${GH_PUSH_USER:-${GITHUB_USER:-}}"
  local github_token="${GH_PUSH_TOKEN:-${GITHUB_TOKEN:-}}"

  if [[ -z "${github_user}" ]]; then
    echo "GH_PUSH_USER or GITHUB_USER is required for GitHub HTTPS push."
    return 1
  fi

  if [[ -z "${github_token}" ]]; then
    echo "GH_PUSH_TOKEN or GITHUB_TOKEN is required for GitHub HTTPS push."
    return 1
  fi

  local askpass
  askpass="$(mktemp)"
  {
    printf '#!/usr/bin/env bash\n'
    printf 'case "$1" in\n'
    printf '  *Username*) printf "%%s\\n" "${GITHUB_HTTPS_USER:?GITHUB_HTTPS_USER is not set}" ;;\n'
    printf '  *Password*) printf "%%s\\n" "${GITHUB_HTTPS_TOKEN:?GITHUB_HTTPS_TOKEN is not set}" ;;\n'
    printf '  *) printf "\\n" ;;\n'
    printf 'esac\n'
  } > "${askpass}"
  chmod 700 "${askpass}"

  export GITHUB_HTTPS_USER="${github_user}"
  export GITHUB_HTTPS_TOKEN="${github_token}"
  export GIT_ASKPASS="${askpass}"
  export GIT_TERMINAL_PROMPT=0
}

function get_github_https_remote() {
  local github_remote="${TARGET_GITHUB_REPO:-https://github.com/espressif/esp-apa.git}"

  case "${github_remote}" in
    git@github.com:*)
      github_remote="https://github.com/${github_remote#git@github.com:}"
      ;;
    ssh://git@github.com/*)
      github_remote="https://github.com/${github_remote#ssh://git@github.com/}"
      ;;
  esac

  if [[ "${github_remote}" != https://github.com/* ]]; then
    echo "GitHub remote must use https://github.com/ for token push: ${github_remote}" >&2
    return 1
  fi

  printf "%s\n" "${github_remote}"
}

function setup_ci_venv() {
  pip install --upgrade pip
  pip install --upgrade "${IDF_CI_INSTALL_SPEC:?IDF_CI_INSTALL_SPEC is not set}"
}

# ===========================================================================
# Auto-discover components/*/test_app and components/*/examples/* (like
# esp-apa-src walks algorithms/*/test_app). Do not add a GitLab job per
# component; one job per chip walks every matching app.
# ===========================================================================

function apa_log() { printf '[apa] %s\n' "$*"; }
function apa_die() { printf '[apa] ERROR: %s\n' "$*" >&2; exit 1; }

function apa_repo_root() {
  if [[ -n "${PROJECT_ROOT:-}" ]]; then
    printf '%s\n' "${PROJECT_ROOT}"
  elif [[ -n "${CI_PROJECT_DIR:-}" ]]; then
    printf '%s\n' "${CI_PROJECT_DIR}"
  else
    git rev-parse --show-toplevel
  fi
}

function apa_build_dir_rel() {
  [[ -n "${IDF_TARGET:-}" ]] || apa_die "IDF_TARGET is not set"
  printf 'build_%s' "${IDF_TARGET}"
}

function apa_idf_build() {
  local -a idf_args=("$@")
  if [[ "${IDF_TARGET}" == "esp32s31" ]]; then
    idf.py --preview "${idf_args[@]}" build
  else
    idf.py "${idf_args[@]}" build
  fi
}

# Optional filter: argument, else $APA_COMPONENT. Empty = every components/*/test_app.
function apa_list_test_apps() {
  local filter="${1:-${APA_COMPONENT:-}}"
  local root app_dir name
  root="$(apa_repo_root)"
  for app_dir in "${root}"/components/*/test_app; do
    [[ -d "${app_dir}" ]] || continue
    [[ -f "${app_dir}/CMakeLists.txt" ]] || continue
    name="$(basename "$(dirname "${app_dir}")")"
    if [[ -n "${filter}" && "${name}" != "${filter}" ]]; then
      continue
    fi
    printf '%s\n' "${app_dir}"
  done
}

# IDF example projects under components/*/examples/<name>.
function apa_list_examples() {
  local root example
  root="$(apa_repo_root)"
  for example in "${root}"/components/*/examples/*; do
    [[ -d "${example}" ]] || continue
    [[ -f "${example}/CMakeLists.txt" ]] || continue
    printf '%s\n' "${example}"
  done
}

# Board name from example/ci.yml for the current IDF_TARGET (bmgr).
# ci.yml keys look like: "  esp32s31: esp32_s31_korvo_1"
function apa_example_board() {
  local example="$1"
  local target="${2:-${IDF_TARGET:-}}"
  local ci="${example}/ci.yml"
  local key val
  [[ -n "${target}" ]] || return 1
  [[ -f "${ci}" ]] || return 1
  while IFS= read -r line || [[ -n "${line}" ]]; do
    [[ "${line}" =~ ^[[:space:]]*# ]] && continue
    [[ "${line}" == *:* ]] || continue
    key="${line%%:*}"
    val="${line#*:}"
    key="${key#"${key%%[![:space:]]*}"}"
    key="${key%"${key##*[![:space:]]}"}"
    val="${val#"${val%%[![:space:]]*}"}"
    val="${val%"${val##*[![:space:]]}"}"
    [[ "${key}" == "${target}" ]] || continue
    [[ -n "${val}" ]] || continue
    printf '%s\n' "${val}"
    return 0
  done < "${ci}"
  return 1
}

function build_test_apps() {
  [[ -n "${IDF_TARGET:-}" ]] || apa_die "IDF_TARGET is not set"
  command -v idf.py >/dev/null || apa_die "idf.py not found (IDF not set up)"

  local app_dir rel_build build_dir found=0
  local root
  root="$(apa_repo_root)"
  rel_build="$(apa_build_dir_rel)"

  while IFS= read -r app_dir; do
    found=1
    build_dir="${app_dir}/${rel_build}"

    rm -f "${app_dir}/sdkconfig" "${app_dir}/sdkconfig.old" "${app_dir}/dependencies.lock"
    rm -rf "${build_dir}"
    mkdir -p "${build_dir}"

    apa_log "build ${app_dir#"${root}"/} target=${IDF_TARGET} build_dir=${rel_build}"
    apa_idf_build \
      -C "${app_dir}" \
      -B "${build_dir}" \
      -DIDF_TARGET="${IDF_TARGET}" \
      -DSDKCONFIG="${build_dir}/sdkconfig"

    apa_log "build OK: ${build_dir}"
  done < <(apa_list_test_apps)

  [[ "${found}" -eq 1 ]] || apa_die "no components/*/test_app found${APA_COMPONENT:+ (APA_COMPONENT=${APA_COMPONENT})}"
}

function build_examples_for_target() {
  [[ -n "${IDF_TARGET:-}" ]] || apa_die "IDF_TARGET is not set"
  command -v idf.py >/dev/null || apa_die "idf.py not found (IDF not set up)"

  local example board rel_build build_dir
  local root
  root="$(apa_repo_root)"
  rel_build="$(apa_build_dir_rel)"

  while IFS= read -r example; do
    board=""
    board="$(apa_example_board "${example}" "${IDF_TARGET}")" || true
    if [[ -z "${board}" ]]; then
      apa_log "skip example ${example#"${root}"/}: no ci.yml board for ${IDF_TARGET}"
      continue
    fi

    build_dir="${example}/${rel_build}"
    rm -rf "${example}/components/gen_bmgr_codes"
    rm -f "${example}/sdkconfig" "${example}/sdkconfig.old" "${example}/dependencies.lock"
    rm -rf "${build_dir}"

    apa_log "bmgr+build ${example#"${root}"/} target=${IDF_TARGET} board=${board}"
    python "${root}/tools/ci/esp_bmgr_pre_build.py" \
      --target "${IDF_TARGET}" \
      --board "${board}" \
      --app-path "${example}"

    mkdir -p "${build_dir}"
    local -a idf_args=(
      -C "${example}"
      -B "${build_dir}"
      -DSDKCONFIG="${build_dir}/sdkconfig"
      -DIDF_TARGET="${IDF_TARGET}"
    )
    if [[ -f "${example}/components/gen_bmgr_codes/board_manager.defaults" ]]; then
      idf_args+=(-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;components/gen_bmgr_codes/board_manager.defaults")
    fi
    apa_idf_build "${idf_args[@]}"
    apa_log "build OK: ${build_dir}"
  done < <(apa_list_examples)
}

function pytest_apps() {
  [[ -n "${IDF_TARGET:-}" ]] || apa_die "IDF_TARGET is not set"

  local app_dir rel_build
  local -a pytest_paths=()
  rel_build="$(apa_build_dir_rel)"
  export ESPBAUD="${ESPBAUD:-115200}"

  while IFS= read -r app_dir; do
    pytest_paths+=("${app_dir}")
  done < <(apa_list_test_apps)

  [[ ${#pytest_paths[@]} -gt 0 ]] || apa_die "no components/*/test_app found${APA_COMPONENT:+ (APA_COMPONENT=${APA_COMPONENT})}"

  apa_log "pytest ${pytest_paths[*]} target=${IDF_TARGET} build_dir=${rel_build}"

  # IDF 6.x target-test images autoload idf-ci. In CI it drops every case
  # whose binary is missing from app_info_*.txt (idf-build-apps). This repo
  # builds with idf.py into build_<target>, so that list is empty and pytest
  # reports "collected 0 items". Keep --target for pytest-embedded flashing;
  # -m still picks the per-chip test function.
  pytest -p no:idf-ci "${pytest_paths[@]}" \
    --override-ini python_files=pytest_*.py \
    --target "${IDF_TARGET}" \
    -m "${IDF_TARGET}" \
    --junitxml=XUNIT_RESULT.xml \
    --embedded-services esp,idf \
    --esptool-baud "${ESPBAUD}" \
    --build-dir "${rel_build}" \
    --parallel-count 1 \
    --parallel-index 1
}

