#!/usr/bin/env bash

set -euo pipefail

#######################
# Configuration
#######################

PROGNAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
BUILD_DIR="${SCRIPT_DIR}/build"
BINARY_NAME="dusth"
BUILD_OUT="${BUILD_DIR}/${BINARY_NAME}"

DEFAULT_INSTALL_DIR="${HOME}/.local/bin"
TERMUX_PREFIX="${PREFIX:-}"
DETECT_TERMUX=0
if [ -n "${TERMUX_PREFIX}" ] && [ -d "${TERMUX_PREFIX}" ]; then
    # Common Termux environment prefix is /data/data/com.termux/files/usr
    if echo "${TERMUX_PREFIX}" | grep -q "com.termux" 2>/dev/null; then
        DETECT_TERMUX=1
    fi
fi

INSTALL_DIR="${DEFAULT_INSTALL_DIR}"
SKIP_BUILD=0
CLEAN_ONLY=0
UNINSTALL_ONLY=0
JOBS=1
STRIP_BINARY=1
VERBOSE=1

CFLAGS_DEFAULT="-std=c11 -O2 -Wall -Wextra -fno-common -pipe"
LDFLAGS_DEFAULT=""
LIBS_DEFAULT="-lm"

#######################
# Helpers / Logging
#######################

log_info() {
    printf "\033[1;34m[INFO]\033[0m %s\n" "$*"
}

log_warn() {
    printf "\033[1;33m[WARN]\033[0m %s\n" "$*"
}

log_error() {
    printf "\033[1;31m[ERROR]\033[0m %s\n" "$*" >&2
}

die() {
    log_error "$*"
    exit 1
}

confirm_yesno() {
    # ask user yes/no, default no
    local prompt="$1"
    read -r -p "$prompt [y/N]: " ans
    case "$ans" in
        [Yy]|[Yy][Ee][Ss]) return 0 ;;
        *) return 1 ;;
    esac
}

which_or_die() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        die "Required command '$cmd' not found in PATH. Please install it and re-run."
    fi
}

#######################
# Argument parsing
#######################

show_help() {
    cat <<EOF
$PROGNAME - Build and install Dusth

Usage:
  $PROGNAME [options]

Options:
  --prefix PATH         Set install prefix (not used directly; recommend --install-dir)
  --install-dir PATH    Set exact installation directory for the dusth binary
                        (default: ${DEFAULT_INSTALL_DIR}, Termux detected: will use \$PREFIX/bin)
  --skip-build          Do not compile, just install existing build/dusth
  --clean               Remove build artifacts and exit
  --uninstall           Remove installed binary and exit
  --jobs N              Use N parallel jobs for compilation (uses nproc/getconf if omitted)
  --no-strip            Do not strip the binary after build
  --verbose             Verbose output
  --quiet               Less verbose
  --help                Show this help and exit

Examples:
  $PROGNAME
  $PROGNAME --install-dir \$HOME/.local/bin
  $PROGNAME --skip-build
  $PROGNAME --clean
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)
            shift
            [ $# -gt 0 ] || die "--prefix requires an argument"
            # Using install-dir instead
            ;;
        --install-dir)
            shift
            [ $# -gt 0 ] || die "--install-dir requires an argument"
            INSTALL_DIR="$1"
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --clean)
            CLEAN_ONLY=1
            shift
            ;;
        --uninstall)
            UNINSTALL_ONLY=1
            shift
            ;;
        --jobs)
            shift
            [ $# -gt 0 ] || die "--jobs requires an argument"
            JOBS="$1"
            shift
            ;;
        --no-strip)
            STRIP_BINARY=0
            shift
            ;;
        --verbose)
            VERBOSE=1
            shift
            ;;
        --quiet)
            VERBOSE=0
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

if [ "$DETECT_TERMUX" -eq 1 ]; then
    # In Termux prefer $PREFIX/bin
    INSTALL_DIR="${TERMUX_PREFIX}/bin"
    log_info "Termux environment detected. Default install dir set to: ${INSTALL_DIR}"
else
    log_info "Install dir: ${INSTALL_DIR}"
fi

#######################
# Commands
#######################

do_clean() {
    log_info "Cleaning build artifacts..."
    rm -rf "${BUILD_DIR}"
    log_info "Clean complete."
}

do_uninstall() {
    log_info "Uninstalling ${BINARY_NAME} from ${INSTALL_DIR}..."
    if [ -f "${INSTALL_DIR}/${BINARY_NAME}" ]; then
        rm -f "${INSTALL_DIR}/${BINARY_NAME}"
        log_info "Removed ${INSTALL_DIR}/${BINARY_NAME}"
    else
        log_warn "No binary found at ${INSTALL_DIR}/${BINARY_NAME}"
    fi
    # try to remove PATH line from common rc files if it was added previously
    for rc in "${HOME}/.profile" "${HOME}/.bashrc" "${HOME}/.bash_profile" "${HOME}/.zshrc"; do
        if [ -f "$rc" ]; then
            if grep -q 'export PATH="$HOME/.local/bin:$PATH"' "$rc" 2>/dev/null; then
                log_info "Removing PATH modification from $rc"
                sed -i.bak '/export PATH="$HOME\/.local\/bin:\$PATH"/d' "$rc" || true
            fi
        fi
    done
    log_info "Uninstall complete."
}

ensure_build_tools() {
    which_or_die gcc
    if [ "$STRIP_BINARY" -eq 1 ]; then
        if ! command -v strip >/dev/null 2>&1; then
            log_warn "strip not found; continuing without stripping binary"
            STRIP_BINARY=0
        fi
    fi
}

detect_jobs() {
    if [ -z "${JOBS:-}" ] || [ "${JOBS}" -le 0 ]; then
        if command -v nproc >/dev/null 2>&1; then
            JOBS="$(nproc)"
        else
            JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
        fi
    fi
    log_info "Using JOBS=${JOBS}"
}

build_project() {
    log_info "Starting build"
    mkdir -p "${BUILD_DIR}"
    detect_jobs
    ensure_build_tools

    local cflags="${CFLAGS_DEFAULT}"
    local ldflags="${LDFLAGS_DEFAULT}"
    local libs="${LIBS_DEFAULT}"

    log_info "Source directory: ${SRC_DIR}"
    log_info "Build output: ${BUILD_OUT}"
    log_info "Compiler flags: ${cflags}"
    log_info "Linker flags: ${ldflags}"

    # compile each C file separately into build/ then link
    local files
    files=$(find "${SRC_DIR}" -maxdepth 1 -type f -name '*.c' -print)
    if [ -z "${files}" ]; then
        die "No C source files found in ${SRC_DIR}"
    fi

    local objlist=()
    for srcfile in ${files}; do
        local base
        base="$(basename "${srcfile}" .c)"
        local obj="${BUILD_DIR}/${base}.o"
        log_info "Compiling ${srcfile} -> ${obj}"
        gcc ${cflags} -MMD -MP -c "${srcfile}" -o "${obj}"
        objlist+=("${obj}")
    done

    log_info "Linking ${BINARY_NAME}"
    gcc ${ldflags} -o "${BUILD_OUT}" "${objlist[@]}" ${libs}

    if [ "${STRIP_BINARY}" -eq 1 ] && command -v strip >/dev/null 2>&1; then
        log_info "Stripping binary to reduce size"
        strip --strip-all "${BUILD_OUT}" || log_warn "strip failed but build succeeded"
    fi

    if [ ! -x "${BUILD_OUT}" ]; then
        die "Build failed: output ${BUILD_OUT} is not executable"
    fi

    log_info "Build finished: ${BUILD_OUT}"
}

verify_binary() {
    if [ ! -x "${BUILD_OUT}" ]; then
        die "Binary not found at ${BUILD_OUT}"
    fi
    log_info "Verifying built binary runs"
    if "${BUILD_OUT}" --version >/dev/null 2>&1; then
        log_info "Binary executed successfully (version check passed)"
    else
        log_warn "Binary executed with non-zero status or produced no output for --version"
    fi
}

perform_install() {
    log_info "Preparing to install to ${INSTALL_DIR}"
    mkdir -p "${INSTALL_DIR}"
    if [ ! -w "${INSTALL_DIR}" ]; then
        log_warn "Install dir ${INSTALL_DIR} is not writable by current user"
    fi

    local target="${INSTALL_DIR}/${BINARY_NAME}"
    if [ -f "${target}" ]; then
        log_info "Existing binary found at ${target}, backing up to ${target}.old"
        mv -f "${target}" "${target}.old" || log_warn "Could not move existing file"
    fi

    cp -f "${BUILD_OUT}" "${target}" || die "Failed to copy binary to ${target}"
    chmod 0755 "${target}" || die "Failed to chmod ${target}"
    log_info "Installed ${target}"

    # Ensure install dir in PATH for common shells
    if ! echo "${PATH}" | tr ':' '\n' | grep -qx "${INSTALL_DIR}"; then
        # Try to add to ~/.profile or ~/.bashrc depending on shell
        local rcfile=""
        if [ -n "${SHELL:-}" ] && echo "${SHELL}" | grep -q "zsh"; then
            rcfile="${HOME}/.zshrc"
        elif [ -f "${HOME}/.bashrc" ]; then
            rcfile="${HOME}/.bashrc"
        else
            rcfile="${HOME}/.profile"
        fi
        if [ -f "${rcfile}" ]; then
            if ! grep -q 'export PATH="$HOME/.local/bin:$PATH"' "${rcfile}" 2>/dev/null; then
                log_info "Adding ${INSTALL_DIR} to PATH in ${rcfile}"
                printf '\n# Added by Dusth installer\nexport PATH="$HOME/.local/bin:$PATH"\n' >> "${rcfile}"
                log_info "Appended PATH export to ${rcfile}. You may need to re-open your shell or run: source ${rcfile}"
            else
                log_info "PATH export already present in ${rcfile}"
            fi
        else
            log_warn "No suitable rcfile found to add PATH entry. You can add this line to your shell rc: export PATH=\"${INSTALL_DIR}:\$PATH\""
        fi
    else
        log_info "Install dir already in PATH"
    fi

    log_info "Installation complete. To run: ${BINARY_NAME}"
}

#######################
# Main flow
#######################

if [ "${CLEAN_ONLY}" -eq 1 ]; then
    do_clean
    exit 0
fi

if [ "${UNINSTALL_ONLY}" -eq 1 ]; then
    do_uninstall
    exit 0
fi

if [ "${SKIP_BUILD}" -eq 0 ]; then
    build_project
else
    if [ ! -x "${BUILD_OUT}" ]; then
        die "SKIP_BUILD specified but ${BUILD_OUT} not found or not executable"
    fi
fi

verify_binary
perform_install

log_info "Done. Run '${BINARY_NAME} --version' or '${BINARY_NAME} <script.dth>' to use Dusth."
