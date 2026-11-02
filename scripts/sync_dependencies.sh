#!/usr/bin/env bash
# Sync / build external dependencies for AniketosHEN.
#
# Embedded payload input: kstuff.elf
# Also built from source: onion_elfldr.elf (private 9020 runtime loader)
# Removed: external elfldr.elf (9021 service), ps5debug, ps5-app-dumper, Byepervisor/hen.bin
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="${ROOT}/third_party"
CACHE="${ONIONHEN_CACHE_DIR:-${ROOT}/.cache/dependencies}"

PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-}"
FROM_SOURCE=0
STUB_MISSING=0
INIT_SUBMODULES=0
FORCE_DOWNLOAD=0

# kstuff-lite v1.11 supports firmware 1.00-13.60 (PS4 fpkg up to 11.60, per
# its release notes). Keep kKstuffMaxFirmware in
# source/bootstrapper/source/bootstrap_safety.cpp in step with this version.
KSTUFF_VERSION="v1.11"
KSTUFF_URL="https://github.com/EchoStretch/kstuff-lite/releases/download/${KSTUFF_VERSION}/kstuff.elf"
# Pinned: an upstream file that changed after review is refused, not embedded.
KSTUFF_SHA256="ab9a6cb4d3b1daf139d4d646e402b1cf569071acd64599c936d7a3a6164dc779"
KSTUFF_SOURCE_DIR="${TP}/kstuff-lite"
# Real release blob is hundreds of KB+; stubs are tiny markers.
KSTUFF_MIN_BYTES=65536

log()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ok]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
Sync AniketosHEN external dependencies from open-source upstreams.

Submodules (under third_party/):
  kstuff-lite      https://github.com/EchoStretch/kstuff-lite
Runtime-only external dependency:
  elfldr @ 9021    https://github.com/ps5-payload-dev/elfldr
                  Required only for initial bootstrap; AniketosHEN starts
                  onion_elfldr.elf @ 9020 for runtime launches.

Removed from AniketosHEN (not synced):
  external elfldr.elf (9021 service), ps5debug, ps5-app-dumper, Byepervisor/hen.bin

Options:
  --init-submodules   git submodule update --init --recursive
  --from-source       Require source checkouts; disable release fallbacks
  --stub-missing      Write tiny placeholders if download/build fails
  --force             Re-download/rebuild external dependencies
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from-source) FROM_SOURCE=1; shift ;;
    --stub-missing) STUB_MISSING=1; shift ;;
    --init-submodules) INIT_SUBMODULES=1; shift ;;
    --force) FORCE_DOWNLOAD=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

download() {
  local url="$1" dest="$2"
  mkdir -p "$(dirname "${dest}")"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 -o "${dest}.tmp" "${url}"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "${dest}.tmp" "${url}"
  else
    return 1
  fi
  mv -f "${dest}.tmp" "${dest}"
}

place() {
  local src="$1" dest="$2"
  mkdir -p "$(dirname "${dest}")"
  cp -f "${src}" "${dest}"
  ok "$(basename "${dest}") <- ${src}"
}

file_sha256() {
  local path="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${path}" | awk '{print $1}'
  else
    shasum -a 256 "${path}" | awk '{print $1}'
  fi
}

download_verified() {
  local url="$1" dest="$2" expected="$3"
  if ! download "${url}" "${dest}"; then
    return 1
  fi
  if [[ "$(file_sha256 "${dest}")" != "${expected}" ]]; then
    warn "checksum mismatch for $(basename "${dest}")"
    rm -f "${dest}"
    return 1
  fi
  return 0
}

stub() {
  local dest="$1" name="$2"
  mkdir -p "$(dirname "${dest}")"
  printf 'AniketosHEN-STUB:%s\0' "${name}" > "${dest}"
  warn "STUB ${dest} (not for real hardware)"
}

need_sdk() {
  [[ -n "${PS5_PAYLOAD_SDK}" && -d "${PS5_PAYLOAD_SDK}" ]] || \
    die "PS5_PAYLOAD_SDK required to build from source"
  export PS5_PAYLOAD_SDK
  export PATH="${PS5_PAYLOAD_SDK}/bin:${PATH}"
}

init_submodules() {
  log "git submodule update --init --recursive"
  git -C "${ROOT}" submodule update --init --recursive
  ok "submodules ready"
}

kstuff_looks_cached() {
  local path="$1"
  [[ -f "${path}" ]] || return 1
  local sz
  sz="$(wc -c < "${path}" | tr -d ' ')"
  # Reject empty / AniketosHEN-STUB placeholders from --stub-missing (and the
  # OnionHEN-STUB ones an older checkout may have left in the cache).
  if [[ "${sz}" -lt "${KSTUFF_MIN_BYTES}" ]]; then
    return 1
  fi
  if head -c 32 "${path}" 2>/dev/null | grep -Eq '(AniketosHEN|OnionHEN)-STUB'; then
    return 1
  fi
  # An older or unreviewed blob in the cache is replaced, not reused.
  [[ "$(file_sha256 "${path}")" == "${KSTUFF_SHA256}" ]]
}

sync_kstuff() {
  local dest="${CACHE}/kstuff.elf"

  # Default path: reuse local blob so every build.sh does not re-hit GitHub.
  if [[ "${FORCE_DOWNLOAD}" -eq 0 && "${FROM_SOURCE}" -eq 0 ]] &&
      kstuff_looks_cached "${dest}"; then
    ok "kstuff.elf already present ($(wc -c < "${dest}" | tr -d ' ') bytes) — skip download"
    return 0
  fi

  if [[ "${FROM_SOURCE}" -eq 0 ]]; then
    log "kstuff: download kstuff-lite ${KSTUFF_VERSION}"
    if download_verified "${KSTUFF_URL}" "${dest}" "${KSTUFF_SHA256}"; then
      ok "kstuff.elf (kstuff-lite ${KSTUFF_VERSION}, sha256 verified)"
      return 0
    fi
    warn "download failed, trying submodule build"
  fi

  if [[ -d "${KSTUFF_SOURCE_DIR}" ]]; then
    need_sdk
    log "kstuff: build third_party/kstuff-lite (best-effort)"
    if [[ -x "${KSTUFF_SOURCE_DIR}/ci-ps5-kstuff-ldr.sh" ]]; then
      (cd "${KSTUFF_SOURCE_DIR}" && bash ./ci-ps5-kstuff-ldr.sh) || true
    elif [[ -f "${KSTUFF_SOURCE_DIR}/Makefile" ]]; then
      make -C "${KSTUFF_SOURCE_DIR}" -j4 || true
    fi
    local found
    found="$(find "${KSTUFF_SOURCE_DIR}" -name 'kstuff.elf' 2>/dev/null | head -1 || true)"
    if [[ -n "${found}" ]]; then
      place "${found}" "${dest}"
      return 0
    fi
  fi

  if [[ "${STUB_MISSING}" -eq 1 ]]; then
    stub "${dest}" "kstuff.elf"
    return 0
  fi
  die "kstuff.elf unavailable (prefer release download)"
}

main() {
  log "AniketosHEN dependency sync"
  echo "  third_party = ${TP}"
  echo "  cache       = ${CACHE}"

  mkdir -p "${CACHE}"

  if [[ "${INIT_SUBMODULES}" -eq 1 ]]; then
    init_submodules
  elif [[ ! -d "${TP}/kstuff-lite" ]]; then
    warn "submodules not checked out — run: git submodule update --init --recursive"
    warn "kstuff release downloads still work without submodules"
  fi

  sync_kstuff

  log "Dependency sync done"
  ls -lah "${CACHE}/kstuff.elf" 2>/dev/null || true
}

main
