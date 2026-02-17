#!/usr/bin/env bash
set -euo pipefail

PROGNAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"
BINARY_NAME="dusth"
BUILD_OUT="${BUILD_DIR}/${BINARY_NAME}"
DEFAULT_INSTALL_DIR="${HOME}/.local/bin"
INSTALL_DIR="${DEFAULT_INSTALL_DIR}"
SKIP_BUILD=0
CLEAN_ONLY=0
UNINSTALL_ONLY=0
JOBS=0
STRIP_BINARY=1
VERBOSE=1
FORCE_INSTALL=0
DRY_RUN=0
MAKE_PACKAGE=0
SIGN_BINARY=0
SIGN_KEY=""
ARCHIVE_FORMAT="tar.gz"
ARCHIVE_NAME="${BINARY_NAME}-package"
EXTRA_CFLAGS=""
EXTRA_LDFLAGS=""
LIBS_DEFAULT="-lm"
CFLAGS_DEFAULT="-std=c11 -O2 -Wall -Wextra -fno-common -pipe"
LDFLAGS_DEFAULT=""
TERMUX_PREFIX="${PREFIX:-}"
DETECT_TERMUX=0
if [ -n "${TERMUX_PREFIX}" ] && [ -d "${TERMUX_PREFIX}" ]; then
  if echo "${TERMUX_PREFIX}" | grep -q "com.termux" 2>/dev/null; then
    DETECT_TERMUX=1
  fi
fi

ESC_RED="\033[1;31m"
ESC_GREEN="\033[1;32m"
ESC_YELLOW="\033[1;33m"
ESC_BLUE="\033[1;34m"
ESC_RESET="\033[0m"

log_info(){ printf "%b[INFO]%b %s\n" "${ESC_BLUE}" "${ESC_RESET}" "$*"; }
log_warn(){ printf "%b[WARN]%b %s\n" "${ESC_YELLOW}" "${ESC_RESET}" "$*"; }
log_error(){ printf "%b[ERROR]%b %s\n" "${ESC_RED}" "${ESC_RESET}" "$*" >&2; }
die(){ log_error "$*"; exit 1; }

confirm_yesno(){ local prompt="$1"; if [ "${FORCE_INSTALL}" -eq 1 ]; then return 0; fi; read -r -p "$prompt [y/N]: " ans; case "$ans" in [Yy]|[Yy][Ee][Ss]) return 0 ;; *) return 1 ;; esac }

which_or_die(){ local cmd="$1"; if ! command -v "$cmd" >/dev/null 2>&1; then die "Required command '$cmd' not found in PATH."; fi }

show_help(){
cat <<EOF
$PROGNAME - Dusth build & installer (heavy-weight, flexible)

Usage:
  $PROGNAME [options]

Options:
  --install-dir PATH    Set install dir (default: ${DEFAULT_INSTALL_DIR})
  --skip-build          Do not compile, just install existing build
  --clean               Remove build artifacts and exit
  --uninstall           Remove installed binary and exit
  --jobs N              Use N parallel jobs for compilation
  --no-strip            Do not strip the binary after build
  --verbose             Verbose output (default)
  --force               Force actions without prompting
  --dry-run             Show actions but do not execute
  --package             Create package (tar.gz or zip)
  --archive-form FORMAT Set archive format: tar.gz or zip
  --sign KEY            Sign binary with gpg KEY
  --help                Show this help and exit

Examples:
  $PROGNAME
  $PROGNAME --install-dir \$HOME/.local/bin
  $PROGNAME --skip-build --package --archive-form zip
EOF
}

parse_args(){
  while [ $# -gt 0 ]; do
    case "$1" in
      --install-dir) shift; [ $# -gt 0 ] || die "--install-dir requires an argument"; INSTALL_DIR="$1"; shift ;;
      --skip-build) SKIP_BUILD=1; shift ;;
      --clean) CLEAN_ONLY=1; shift ;;
      --uninstall) UNINSTALL_ONLY=1; shift ;;
      --jobs) shift; [ $# -gt 0 ] || die "--jobs requires an argument"; JOBS="$1"; shift ;;
      --no-strip) STRIP_BINARY=0; shift ;;
      --verbose) VERBOSE=1; shift ;;
      --force) FORCE_INSTALL=1; shift ;;
      --dry-run) DRY_RUN=1; shift ;;
      --package) MAKE_PACKAGE=1; shift ;;
      --archive-form) shift; [ $# -gt 0 ] || die "--archive-form requires an argument"; ARCHIVE_FORMAT="$1"; shift ;;
      --sign) shift; [ $# -gt 0 ] || die "--sign requires a key id"; SIGN_BINARY=1; SIGN_KEY="$1"; shift ;;
      --help|-h) show_help; exit 0 ;;
      *) die "Unknown option: $1" ;;
    esac
  done
}

if [ "${DETECT_TERMUX}" -eq 1 ]; then
  INSTALL_DIR="${TERMUX_PREFIX}/bin"
  log_info "Termux detected. Using install dir: ${INSTALL_DIR}"
else
  log_info "Install dir: ${INSTALL_DIR}"
fi

resolve_src_path(){
  if [ -d "${SRC_DIR}" ]; then
    echo "${SRC_DIR}"
    return 0
  fi
  if [ -d "${SCRIPT_DIR}/source" ]; then
    SRC_DIR="${SCRIPT_DIR}/source"
    echo "${SRC_DIR}"
    return 0
  fi
  if [ -d "${SCRIPT_DIR}/src" ]; then
    echo "${SCRIPT_DIR}/src"
    return 0
  fi
  die "No src directory found. Place C sources in ${SCRIPT_DIR}/src"
}

ensure_build_tools(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: ensure_build_tools"; return; fi
  if command -v gcc >/dev/null 2>&1; then CC=gcc
  elif command -v clang >/dev/null 2>&1; then CC=clang
  else die "No C compiler found (gcc or clang)."
  fi
  if [ "${STRIP_BINARY}" -eq 1 ]; then
    if ! command -v strip >/dev/null 2>&1; then log_warn "strip not found; disabling stripping"; STRIP_BINARY=0; fi
  fi
  if command -v ar >/dev/null 2>&1; then :; else log_warn "ar not found; some packaging may be limited"; fi
}

detect_jobs(){
  if [ -z "${JOBS:-}" ] || [ "${JOBS}" -le 0 ]; then
    if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"; else JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"; fi
  fi
  log_info "Using JOBS=${JOBS}"
}

spinner_start(){
  MSG="$1"
  ( while :; do for c in '-' '\' '|' '/'; do printf "\r${ESC_GREEN}%s${ESC_RESET} %s %s" "$MSG" "$c" "$(date +%T)"; sleep 0.08; done; done ) &
  SPINNER_PID=$!
  disown
}
spinner_stop(){
  if [ -n "${SPINNER_PID:-}" ]; then kill "${SPINNER_PID}" 2>/dev/null || true; unset SPINNER_PID; printf "\r"; fi
}

progress_bar(){
  total="$1"; current="$2"
  width=40
  pct=$(( current * 100 / total ))
  filled=$(( width * current / total ))
  empty=$(( width - filled ))
  printf "["
  i=0
  while [ $i -lt $filled ]; do printf "="; i=$((i+1)); done
  j=0
  while [ $j -lt $empty ]; do printf " "; j=$((j+1)); done
  printf "] %3s%%\r" "${pct}"
}

gather_sources(){
  SRC=$(resolve_src_path)
  mapfile -t CFILES < <(find "${SRC}" -maxdepth 1 -type f -name '*.c' | sort)
  mapfile -t HFILES < <(find "${SRC}" -maxdepth 1 -type f -name '*.h' | sort)
  if [ "${#CFILES[@]}" -eq 0 ]; then die "No .c files found in ${SRC}"; fi
}

compile_one(){
  srcfile="$1"
  base="$(basename "$srcfile" .c)"
  obj="${BUILD_DIR}/${base}.o"
  mkdir -p "${BUILD_DIR}"
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: compile ${srcfile} -> ${obj}"; return; fi
  log_info "Compiling ${srcfile}"
  "$CC" ${CFLAGS_DEFAULT} ${EXTRA_CFLAGS} -MMD -MP -c "${srcfile}" -o "${obj}"
}

link_all(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: linking"; return; fi
  objs=( $(for f in "${CFILES[@]}"; do printf "%s\n" "${BUILD_DIR}/$(basename "${f}" .c).o"; done) )
  for o in "${objs[@]}"; do if [ ! -f "${o}" ]; then die "Missing object ${o}"; fi; done
  log_info "Linking to ${BUILD_OUT}"
  "$CC" ${LDFLAGS_DEFAULT} ${EXTRA_LDFLAGS} -o "${BUILD_OUT}" "${objs[@]}" ${LIBS_DEFAULT}
}

strip_if_needed(){
  if [ "${STRIP_BINARY}" -eq 1 ] && command -v strip >/dev/null 2>&1; then
    if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: strip ${BUILD_OUT}"; return; fi
    log_info "Stripping binary"
    strip --strip-all "${BUILD_OUT}" || log_warn "strip failed"
  fi
}

verify_run(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: verify run"; return; fi
  if [ ! -x "${BUILD_OUT}" ]; then die "Binary not found at ${BUILD_OUT}"; fi
  if "${BUILD_OUT}" --version >/dev/null 2>&1; then log_info "Version check OK"; else log_warn "Version check failed or no output"; fi
}

smoke_test(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: smoke test"; return; fi
  tmp="${BUILD_DIR}/.smoke_test.dth"
  printf 'print("smoke_ok")\n' > "${tmp}"
  if "${BUILD_OUT}" "${tmp}" >/dev/null 2>&1; then log_info "Smoke test passed"; else log_warn "Smoke failed"; fi
  rm -f "${tmp}" || true
}

size_and_stats(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: size_and_stats"; return; fi
  if [ -f "${BUILD_OUT}" ]; then
    sz=$(wc -c < "${BUILD_OUT}")
    log_info "Binary size: ${sz} bytes"
    if command -v size >/dev/null 2>&1; then size "${BUILD_OUT}" || true; fi
    if command -v readelf >/dev/null 2>&1; then readelf -h "${BUILD_OUT}" || true; fi
  fi
}

generate_checksums(){
  mkdir -p "${DIST_DIR}"
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: generate_checksums"; return; fi
  if [ -f "${BUILD_OUT}" ]; then
    sha256sum "${BUILD_OUT}" > "${DIST_DIR}/${BINARY_NAME}.sha256"
    log_info "Checksum written to ${DIST_DIR}/${BINARY_NAME}.sha256"
  fi
}

create_package(){
  mkdir -p "${DIST_DIR}"
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: create_package ${ARCHIVE_FORMAT}"; return; fi
  case "${ARCHIVE_FORMAT}" in
    tar.gz) tar -C "${BUILD_DIR}" -czf "${DIST_DIR}/${ARCHIVE_NAME}.tar.gz" "${BINARY_NAME}" ;;
    zip) (cd "${BUILD_DIR}" && zip -r "${DIST_DIR}/${ARCHIVE_NAME}.zip" "${BINARY_NAME}") ;;
    *) die "Unsupported archive format: ${ARCHIVE_FORMAT}" ;;
  esac
  log_info "Package created at ${DIST_DIR}"
}

sign_binary(){
  if [ "${SIGN_BINARY}" -eq 0 ]; then return; fi
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: sign_binary"; return; fi
  if ! command -v gpg >/dev/null 2>&1; then log_warn "gpg not found; skipping signing"; return; fi
  if [ -n "${SIGN_KEY}" ]; then gpg --output "${BUILD_OUT}.sig" --detach-sign --local-user "${SIGN_KEY}" "${BUILD_OUT}" && log_info "Signed with ${SIGN_KEY}" || log_warn "Signing failed"
  else gpg --output "${BUILD_OUT}.sig" --detach-sign "${BUILD_OUT}" && log_info "Signed with default key" || log_warn "Signing failed"
  fi
}

install_binary(){
  mkdir -p "${INSTALL_DIR}"
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: install ${BUILD_OUT} -> ${INSTALL_DIR}"; return; fi
  target="${INSTALL_DIR}/${BINARY_NAME}"
  if [ -f "${target}" ]; then mv -f "${target}" "${target}.old" || log_warn "failed to backup ${target}"; fi
  cp -f "${BUILD_OUT}" "${target}" || die "Failed to copy binary"
  chmod 0755 "${target}" || die "Failed to chmod"
  log_info "Installed ${target}"
  add_path_if_missing
}

add_path_if_missing(){
  if ! echo "${PATH}" | tr ':' '\n' | grep -qx "${INSTALL_DIR}"; then
    rcfile=""
    if [ -n "${SHELL:-}" ] && echo "${SHELL}" | grep -q "zsh"; then rcfile="${HOME}/.zshrc"
    elif [ -f "${HOME}/.bashrc" ]; then rcfile="${HOME}/.bashrc"
    else rcfile="${HOME}/.profile"
    fi
    if [ -f "${rcfile}" ]; then
      if ! grep -q "${INSTALL_DIR}" "${rcfile}" 2>/dev/null; then
        printf '\nexport PATH="%s:$PATH"\n' "${INSTALL_DIR}" >> "${rcfile}"
        log_info "Added ${INSTALL_DIR} to PATH in ${rcfile}"
      fi
    else
      log_warn "No rc file found; add export PATH=\"${INSTALL_DIR}:\$PATH\" to your shell"
    fi
  else
    log_info "Install dir already in PATH"
  fi
}

build_project(){
  log_info "Starting build"
  gather_sources
  detect_jobs
  ensure_build_tools
  total="${#CFILES[@]}"
  count=0
  mkdir -p "${BUILD_DIR}"
  spinner_start "Compiling"
  for src in "${CFILES[@]}"; do
    count=$((count+1))
    compile_one "${src}" &
    pid=$!
    wait "${pid}"
    progress_bar "${total}" "${count}"
  done
  spinner_stop
  printf "\n"
  spinner_start "Linking"
  link_all
  spinner_stop
  strip_if_needed
  verify_run
  smoke_test
  size_and_stats
  generate_checksums
  log_info "Build done: ${BUILD_OUT}"
}

do_clean(){
  log_info "Cleaning build and dist directories"
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: rm -rf ${BUILD_DIR} ${DIST_DIR}"; return; fi
  rm -rf "${BUILD_DIR}" "${DIST_DIR}"
  log_info "Clean complete"
}

do_uninstall(){
  log_info "Uninstalling ${BINARY_NAME}"
  if [ -f "${INSTALL_DIR}/${BINARY_NAME}" ]; then
    if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: rm -f ${INSTALL_DIR}/${BINARY_NAME}"; else rm -f "${INSTALL_DIR}/${BINARY_NAME}"; fi
    log_info "Removed ${INSTALL_DIR}/${BINARY_NAME}"
  else
    log_warn "Nothing to uninstall at ${INSTALL_DIR}/${BINARY_NAME}"
  fi
}

print_repo_stats(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: print_repo_stats"; return; fi
  if command -v cloc >/dev/null 2>&1; then cloc --quiet "${SRC_DIR}" || true
  else
    total_lines=0
    for f in "${CFILES[@]}" "${HFILES[@]}"; do
      if [ -f "$f" ]; then
        lines=$(wc -l < "$f" || echo 0)
        total_lines=$((total_lines + lines))
      fi
    done
    log_info "Approx total source lines: ${total_lines}"
  fi
}

generate_manpage(){
  manfile="${BUILD_DIR}/${BINARY_NAME}.1"
  mkdir -p "${BUILD_DIR}"
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: generate manpage ${manfile}"; return; fi
  cat > "${manfile}" <<MAN
.TH ${BINARY_NAME} 1 "Generated" "dusth"
.SH NAME
${BINARY_NAME} \\- Dusth language interpreter
.SH SYNOPSIS
${BINARY_NAME} [options] <script>
.SH DESCRIPTION
Dusth interpreter generated manpage.
.SH OPTIONS
.TP
\\fB--help\\fR
Show help
MAN
  gzip -f "${manfile}"
  log_info "Manpage generated ${manfile}.gz"
}

install_completions(){
  if [ "${DRY_RUN}" -eq 1 ]; then log_info "DRY RUN: install shell completions"; return; fi
  comp_dir="${HOME}/.local/share/${BINARY_NAME}/completions"
  mkdir -p "${comp_dir}"
  cat > "${comp_dir}/${BINARY_NAME}.bash" <<'BASHC'
_complete_dusth() {
  local cur prev opts
  COMPREPLY=()
  cur="${COMP_WORDS[COMP_CWORD]}"
  opts="--help --version --install --run --debug"
  COMPREPLY=( $(compgen -W "${opts}" -- ${cur}) )
  return 0
}
complete -F _complete_dusth dusth
BASHC
  cat > "${comp_dir}/${BINARY_NAME}.zsh" <<'ZSHC'
#compdef dusth
_arguments "--help" "--version" "--install" "--run" "--debug"
ZSHC
  log_info "Shell completions installed to ${comp_dir}"
}

multi_platform_notes(){
  printf "%b\n" "${ESC_YELLOW}Platform notes:${ESC_RESET}"
  printf "%b\n" " - For Windows build use mingw cross-compiler or build on MSYS2/WSL"
  printf "%b\n" " - For macOS consider using clang and codesign if distributing"
  printf "%b\n" " - For Termux use ${TERMUX_PREFIX}/bin as install dir"
}

main(){
  parse_args "$@"
  if [ "${CLEAN_ONLY}" -eq 1 ]; then do_clean; exit 0; fi
  if [ "${UNINSTALL_ONLY}" -eq 1 ]; then do_uninstall; exit 0; fi
  gather_sources
  if [ "${SKIP_BUILD}" -eq 0 ]; then build_project; else
    if [ ! -x "${BUILD_OUT}" ]; then die "SKIP_BUILD specified but no executable at ${BUILD_OUT}"; fi
  fi
  if [ "${SIGN_BINARY}" -eq 1 ]; then sign_binary; fi
  if [ "${MAKE_PACKAGE}" -eq 1 ]; then create_package; fi
  install_binary
  print_repo_stats
  generate_manpage
  install_completions
  multi_platform_notes
  log_info "Done. Try: ${BINARY_NAME} --version"
}

main "$@"