#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="${GITHUB_REPOSITORY:-xS3Cx/AniketosHEN}"
API="https://api.github.com/repos/${REPO}"
TOKEN="${GITHUB_TOKEN:-}"

usage() {
  cat <<'EOF'
Usage: scripts/release_hen.sh <version> [summary|summary-file]

Builds AniketosHEN Release, generates update.json and publishes the GitHub
release (tag v<version>, marked latest) through the REST API.
GITHUB_TOKEN is required. NOTES_FILE (default release-notes/next.md) is the
Markdown release notes; "## Added" / "## Fixed" sections with "- " items are what
the Discord bot announces. Without it the notes are the summary.
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
log() { printf '==> %s\n' "$*"; }

[[ $# -ge 1 && $# -le 2 ]] || { usage; exit 1; }
VERSION="$1"
CHANNEL="stable"
SUMMARY_ARG="${2:-}"
# Crash reports reach the project's relay unless the caller names another.
export ANIKETOSHEN_REPORT_URL="${ANIKETOSHEN_REPORT_URL-http://wanda227.mikrus.xyz:20227/aniketoshen/report}"

[[ "$VERSION" =~ ^[0-9A-Za-z][0-9A-Za-z._+-]*$ ]] || die "invalid version: $VERSION"
[[ ${#VERSION} -le 32 ]] || die "version is longer than 32 characters"
[[ "$VERSION" =~ [0-9] ]] || die "version must contain a digit"
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
NOTES=""
# The notes gathered since the last release, unless the caller names a file.
if [[ -z "${NOTES_FILE:-}" && -s "${ROOT}/release-notes/next.md" ]]; then
  NOTES_FILE="${ROOT}/release-notes/next.md"
fi
if [[ -n "${NOTES_FILE:-}" ]]; then
  [[ -f "$NOTES_FILE" ]] || die "NOTES_FILE not found: $NOTES_FILE"
  NOTES="$(cat "$NOTES_FILE")"
fi
[[ -n "$NOTES" ]] || NOTES="$SUMMARY"

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

python3 - "$RELEASE_TAG" "$VERSION" "$PRERELEASE" "$NOTES" > "${STAGE}/release.json" <<'PY'
import json
import sys
tag, version, prerelease, notes = sys.argv[1:]
data = {
    "tag_name": tag,
    "name": f"AniketosHEN {version}",
    "body": notes,
    "draft": False,
    "prerelease": prerelease == "true",
}
json.dump(data, sys.stdout, ensure_ascii=False, indent=2)
sys.stdout.write("\n")
PY

CURL=(curl -sS -f -H "Authorization: Bearer ${TOKEN}" -H "Accept: application/vnd.github+json" -H "X-GitHub-Api-Version: 2022-11-28")

RELEASE_JSON="${STAGE}/release-response.json"
RELEASE_ID=""
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
