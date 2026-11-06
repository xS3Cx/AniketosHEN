#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="${GITHUB_REPOSITORY:-xS3Cx/AniketosHEN}"
API="https://api.github.com/repos/${REPO}"
TOKEN="${GITHUB_TOKEN:-}"

usage() {
  cat <<'EOF'
Usage: scripts/release_hen.sh <version> <stable|beta> [summary|summary-file]

Builds AniketosHEN Release, generates update.json and publishes the GitHub
release through the REST API. GITHUB_TOKEN is required.
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
log() { printf '==> %s\n' "$*"; }

[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 1; }
VERSION="$1"
CHANNEL="$2"
SUMMARY_ARG="${3:-}"

[[ "$VERSION" =~ ^[0-9A-Za-z][0-9A-Za-z._+-]*$ ]] || die "invalid version: $VERSION"
[[ ${#VERSION} -le 32 ]] || die "version is longer than 32 characters"
[[ "$VERSION" =~ [0-9] ]] || die "version must contain a digit"
[[ "$CHANNEL" == "stable" || "$CHANNEL" == "beta" ]] || die "channel must be stable or beta"
[[ -n "$TOKEN" ]] || die "GITHUB_TOKEN is required"
command -v curl >/dev/null 2>&1 || die "curl is required"
command -v python3 >/dev/null 2>&1 || die "python3 is required"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is required"
[[ -n "${PS5_PAYLOAD_SDK:-}" ]] || die "PS5_PAYLOAD_SDK is required"

if [[ -n "$SUMMARY_ARG" && -f "$SUMMARY_ARG" ]]; then
  SUMMARY="$(tr '\n' ' ' < "$SUMMARY_ARG" | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')"
else
  SUMMARY="$SUMMARY_ARG"
fi
[[ -n "$SUMMARY" ]] || SUMMARY="AniketosHEN ${VERSION}"

if [[ ${#SUMMARY} -gt 300 ]]; then
  SUMMARY="${SUMMARY:0:300}"
fi

TAG="v${VERSION}"
RELEASE_TAG="$TAG"
PRERELEASE="false"
if [[ "$CHANNEL" == "beta" ]]; then
  RELEASE_TAG="beta"
  PRERELEASE="true"
fi

ASSET_BASE="https://github.com/${REPO}/releases/download/${RELEASE_TAG}"
ASSET_URL="${ASSET_BASE}/AniketosHEN.elf"

log "Building Release ${VERSION} (${CHANNEL})"
(
  cd "$ROOT"
  ANIKETOSHEN_VERSION="$VERSION" \
    ./scripts/build.sh --release --jobs "${JOBS:-$(nproc)}"
)

ELF="${ROOT}/build/bin/AniketosHEN.elf"
[[ -f "$ELF" ]] || die "missing ${ELF}"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp "$ELF" "${STAGE}/AniketosHEN.elf"
(
  cd "$STAGE"
  sha256sum AniketosHEN.elf > AniketosHEN.elf.sha256
)

SIZE="$(stat -c '%s' "${STAGE}/AniketosHEN.elf")"
SHA256="$(sha256sum "${STAGE}/AniketosHEN.elf" | awk '{print $1}')"

if [[ -n "${SOURCE_DATE_EPOCH:-}" ]]; then
  PUBLISHED_AT="$(date -u -d "@${SOURCE_DATE_EPOCH}" +%Y-%m-%dT%H:%M:%SZ)"
else
  PUBLISHED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
fi

python3 - "$VERSION" "$TAG" "$CHANNEL" "$PUBLISHED_AT" "$ASSET_URL" "$SHA256" "$SIZE" "$SUMMARY" \
  > "${STAGE}/update.json" <<'PY'
import json
import sys

version, tag, channel, published_at, url, sha256, size, summary = sys.argv[1:]
data = {
    "version": version,
    "tag": tag,
    "channel": channel,
    "published_at": published_at,
    "url": url,
    "sha256": sha256,
    "size": int(size),
    "summary": summary,
}
json.dump(data, sys.stdout, ensure_ascii=False, indent=2)
sys.stdout.write("\n")
PY

python3 - "$RELEASE_TAG" "$VERSION" "$PRERELEASE" "$SUMMARY" > "${STAGE}/release.json" <<'PY'
import json
import sys

tag, version, prerelease, summary = sys.argv[1:]
data = {
    "tag_name": tag,
    "name": f"AniketosHEN {version}" if tag != "beta" else "AniketosHEN beta",
    "body": summary,
    "draft": False,
    "prerelease": prerelease == "true",
}
json.dump(data, sys.stdout, ensure_ascii=False, indent=2)
sys.stdout.write("\n")
PY

CURL=(curl -sS -f -H "Authorization: Bearer ${TOKEN}" -H "Accept: application/vnd.github+json" -H "X-GitHub-Api-Version: 2022-11-28")

RELEASE_JSON="${STAGE}/release-response.json"
RELEASE_ID=""
if [[ "$CHANNEL" == "beta" ]]; then
  log "Replacing existing beta release assets"
  HTTP_CODE="$(curl -sS -o "$RELEASE_JSON" -w '%{http_code}' \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "${API}/releases/tags/beta")"
  if [[ "$HTTP_CODE" == "200" ]]; then
    RELEASE_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$RELEASE_JSON")"
    mapfile -t ASSET_NAMES < <(python3 -c 'import json,sys; [print(a["id"], a["name"]) for a in json.load(open(sys.argv[1])).get("assets", [])]' "$RELEASE_JSON")
    for entry in "${ASSET_NAMES[@]:-}"; do
      [[ -n "$entry" ]] || continue
      asset_id="${entry%% *}"
      asset_name="${entry#* }"
      case "$asset_name" in
        AniketosHEN.elf|AniketosHEN.elf.sha256|update.json)
          log "Deleting asset ${asset_name}"
          "${CURL[@]}" -X DELETE "${API}/releases/assets/${asset_id}" >/dev/null
          ;;
      esac
    done
  else
    RELEASE_ID=""
  fi
fi

if [[ -z "$RELEASE_ID" ]]; then
  log "Creating release ${RELEASE_TAG}"
  "${CURL[@]}" -X POST -H 'Content-Type: application/json' \
    --data-binary "@${STAGE}/release.json" \
    "${API}/releases" > "$RELEASE_JSON"
  RELEASE_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$RELEASE_JSON")"
fi

UPLOAD_URL="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["upload_url"].split("{")[0])' "$RELEASE_JSON")"

for asset in AniketosHEN.elf AniketosHEN.elf.sha256 update.json; do
  log "Uploading ${asset}"
  "${CURL[@]}" -X POST -H 'Content-Type: application/octet-stream' \
    --data-binary "@${STAGE}/${asset}" \
    "${UPLOAD_URL}?name=${asset}" >/dev/null
done

printf '\nRelease published: https://github.com/%s/releases/tag/%s\n' "$REPO" "$RELEASE_TAG"
printf 'Manifest: %s\n' "$ASSET_BASE/update.json"
