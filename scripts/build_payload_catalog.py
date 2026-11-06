#!/usr/bin/env python3
"""Build AniketosHEN's curated Payload Center catalog and icon sheet.

Only the Python standard library is used (urllib, json, hashlib, zlib,
struct, base64). For GitHub projects the latest release is queried and the
matching ``.elf`` asset is downloaded; non-GitHub projects use ``direct_url``.
Each payload binary is downloaded once to compute its SHA-256 checksum.

Environment:
    GITHUB_TOKEN   optional token, raises the GitHub API rate limit.
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import struct
import sys
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path

USER_AGENT = "AniketosHEN-payload-catalog/1.0"
GITHUB_API = "https://api.github.com"
TIMEOUT_SECONDS = 120

CATEGORY_COLORS = {
    "Servers": (0x2F, 0x6F, 0xED),
    "Storage & Files": (0x0F, 0x9D, 0x58),
    "Debug & Tools": (0xD9, 0x77, 0x06),
    "Management & System": (0x7C, 0x3A, 0xED),
}

# id, GitHub repo or direct URL, category, description, asset regex.
CURATED = [
    # Servers
    dict(id="ftpsrv", repo="ps5-payload-dev/ftpsrv", category="Servers",
         pattern=r"^ftpsrv-ps5\.elf$",
         description="FTP server for PlayStation 5."),
    dict(id="ftpsrv-drakmor", repo="drakmor/ftpsrv", category="Servers",
         pattern=r"^ftpsrv-ps5\.elf$",
         description="Feature-rich FTPS server for PlayStation 5."),
    dict(id="zftpd", repo="seregonwar/zftpd", category="Servers",
         pattern=r"^zftpd-ps5-v.*\.elf$",
         description="Multi-service FTP and web file server."),
    dict(id="websrv", repo="ps5-payload-dev/websrv", category="Servers",
         pattern=r"^websrv-ps5\.elf$",
         description="Web server with a package and homebrew browser."),
    dict(id="shsrv", repo="ps5-payload-dev/shsrv", category="Servers",
         pattern=r"^shsrv-ps5\.elf$",
         description="Shell command server for PlayStation 5."),
    dict(id="klogsrv", repo="ps5-payload-dev/klogsrv", category="Servers",
         pattern=r"^klogsrv-ps5\.elf$",
         description="Kernel log streaming server."),
    dict(id="nanoDNS", repo="drakmor/nanoDNS", category="Servers",
         pattern=r"^nanodns\.elf$",
         description="Lightweight DNS server for PlayStation 5."),
    # Storage & files
    dict(id="ShadowMountPlus", repo="drakmor/ShadowMountPlus",
         category="Storage & Files", pattern=r"^shadowmountplus\.elf$",
         description="Mounts external storage and image files."),
    dict(id="ps4mount", repo="gasb07/ps4mount", category="Storage & Files",
         pattern=r"^ps4mount\.elf$",
         description="Mounts PS4 storage images on PlayStation 5."),
    dict(id="ps5upload", repo="phantomptr/ps5upload",
         category="Storage & Files", pattern=r"^ps5upload-[0-9].*\.elf$",
         description="File transfer service for PlayStation 5."),
    dict(id="ps5-web-file-manager", repo="owendswang/ps5-web-file-manager",
         category="Storage & Files", pattern=r"^web-file-mgr-.*\.elf$",
         description="Browser-based file manager for PlayStation 5."),
    dict(id="BFpilot", repo="ItsBlurf/BFpilot", category="Storage & Files",
         pattern=r"^bfpilot\.elf$",
         description="File browser and transfer helper."),
    dict(id="ps5-app-dumper", repo="EchoStretch/ps5-app-dumper",
         category="Storage & Files", pattern=r"^ps5-app-dumper_.*\.elf$",
         description="Dumps installed PlayStation 5 applications."),
    dict(id="garlic-savemgr",
         direct_url="https://git.etawen.dev/earthonion/garlic-savemgr/releases/"
                    "download/v1.15-beta12/garlic-savemgr.elf",
         source="https://git.etawen.dev/earthonion/garlic-savemgr/releases",
         version="v1.15-beta12", last_update="2026-11-06",
         filename="garlic-savemgr.elf", category="Storage & Files",
         description="Save file manager for PlayStation 5."),
    # Debug & tools
    dict(id="ps5debug-NG", repo="Pharaoh2k/ps5debug-NG",
         category="Debug & Tools", pattern=r"^ps5debug-NG_.*\.elf$",
         description="Debugger for PlayStation 5 homebrew."),
    dict(id="ps5-exporter", repo="Marice/ps5-exporter",
         category="Debug & Tools", pattern=r"^ps5-exporter\.elf$",
         description="Prometheus metrics exporter for PlayStation 5."),
    dict(id="BackPork", repo="BestPig/BackPork", category="Debug & Tools",
         pattern=r"^ps5-backpork\.elf$",
         description="Backport helper for PlayStation 5 titles."),
    dict(id="PS5Save", repo="heni0xyz/PS5Save", category="Debug & Tools",
         pattern=r"^backup\.elf$",
         description="Save data backup tool for PlayStation 5."),
    dict(id="ps5-date-time-sync", repo="kerrdec97/ps5-date-time-sync",
         category="Debug & Tools", pattern=r"^ps5-date-time-sync\.elf$",
         description="Keeps the console clock synchronized."),
    dict(id="WelcomeHubActivator", repo="ghost1372/WelcomeHubActivator",
         category="Debug & Tools", pattern=r"^WelcomeHubActivator-.*\.elf$",
         description="Activates the PlayStation 5 Welcome Hub."),
    dict(id="ps5-dump-forge", repo="quer3q/ps5-dump-forge",
         category="Debug & Tools", pattern=r"^ps5-dump-forge-.*-ps5\.elf$",
         description="Game dumping and inspection toolkit."),
    # Management & system
    dict(id="pldmgr", repo="itsPLK/ps5-payload-manager",
         category="Management & System", pattern=r"^pldmgr_.*\.elf$",
         description="Payload manager and launcher."),
    dict(id="PKG-Manager", repo="itsPLK/ps5-pkg-manager",
         category="Management & System", pattern=r"^pkg-manager_.*\.elf$",
         description="Package install and management tool."),
    dict(id="Lapy-JB-Daemon", repo="itsPLK/PS5-Lapy-JB-Daemon",
         category="Management & System", pattern=r"^lapy_jb_daemon-.*\.elf$",
         description="Lapy jailbreak daemon."),
    dict(id="WebKit-Autoloader-Installer",
         repo="itsPLK/ps5-webkit-autoloader",
         category="Management & System",
         pattern=r"^webkit-autoloader-installer_.*\.elf$",
         description="WebKit autoloader installer."),
    dict(id="kstuff-lite", repo="EchoStretch/kstuff-lite",
         category="Management & System", pattern=r"^kstuff\.elf$",
         description="Kernel exploit helper (kstuff)."),
    dict(id="ps-patch-system", repo="illusionyy/ps-patch-system",
         category="Management & System",
         pattern=r"^patch-bundle-loader-prospero\.elf$",
         description="Prospero patch bundle loader."),
]

FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "J": ("00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
}


def request(url: str, *, headers: dict[str, str] | None = None) -> bytes:
    merged = {"User-Agent": USER_AGENT}
    if headers:
        merged.update(headers)
    for attempt in range(3):
        try:
            with urllib.request.urlopen(
                urllib.request.Request(url, headers=merged),
                timeout=TIMEOUT_SECONDS,
            ) as response:
                return response.read()
        except urllib.error.HTTPError:
            if attempt == 2:
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
        time.sleep(1 + attempt)
    raise RuntimeError("unreachable")


def github_headers() -> dict[str, str]:
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        return {"Authorization": f"Bearer {token}",
                "Accept": "application/vnd.github+json"}
    return {"Accept": "application/vnd.github+json"}


def latest_release(repo: str) -> dict:
    body = request(f"{GITHUB_API}/repos/{repo}/releases/latest",
                   headers=github_headers())
    return json.loads(body)


def pick_asset(release: dict, pattern: str) -> dict:
    regex = re.compile(pattern, re.IGNORECASE)
    assets = [
        asset for asset in release.get("assets") or []
        if str(asset.get("name", "")).lower().endswith(".elf")
        and regex.search(str(asset.get("name", "")))
    ]
    if not assets:
        names = [asset.get("name") for asset in release.get("assets") or []]
        raise SystemExit(
            f"no .elf asset matching {pattern!r} in {release.get('html_url')}: "
            f"{names}"
        )
    assets.sort(key=lambda asset: asset["name"])
    return assets[0]


def create_png(letter: str, color: tuple[int, int, int]) -> bytes:
    width = height = 128
    pixels = bytearray([0, 0, 0, 255] * (width * height))
    r, g, b = color
    dark = (max(r - 45, 0), max(g - 45, 0), max(b - 45, 0))
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 4
            edge = x < 6 or y < 6 or x >= width - 6 or y >= height - 6
            corner = (x < 18 or x >= width - 18) and (
                y < 18 or y >= height - 18
            )
            if corner and (x < 6 or y < 6 or x >= width - 6 or y >= height - 6):
                continue
            cr, cg, cb = dark if edge else color
            pixels[offset:offset + 4] = bytes((cr, cg, cb, 255))

    glyph = FONT.get(letter.upper(), FONT["P"])
    scale = 12
    glyph_width = 5 * scale
    glyph_height = 7 * scale
    left = (width - glyph_width) // 2
    top = (height - glyph_height) // 2
    for row, bits in enumerate(glyph):
        for col, bit in enumerate(bits):
            if bit != "1":
                continue
            for dy in range(scale):
                for dx in range(scale):
                    x = left + col * scale + dx
                    y = top + row * scale + dy
                    offset = (y * width + x) * 4
                    pixels[offset:offset + 4] = bytes((255, 255, 255, 255))

    raw = b"".join(b"\x00" + bytes(pixels[y * width * 4:(y + 1) * width * 4])
                   for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


def build_entry(item: dict) -> dict:
    if item.get("direct_url"):
        url = item["direct_url"]
        filename = item.get("filename") or url.rsplit("/", 1)[-1]
        source = item.get("source") or url
        version = item.get("version", "")
        last_update = item.get("last_update", "")
        release_url = source
    else:
        release = latest_release(item["repo"])
        asset = pick_asset(release, item["pattern"])
        url = asset["browser_download_url"]
        filename = asset["name"]
        # Stable release page, not the tag URL: the tag changes every release
        # and would change the catalog match key used for update detection.
        source = f"https://github.com/{item['repo']}/releases"
        version = release.get("tag_name", "")
        last_update = str(release.get("published_at", ""))[:10]
        release_url = release["html_url"]
    payload = request(url)
    if not payload.startswith(b"\x7fELF"):
        raise SystemExit(f"{item['id']}: {url} is not an ELF file")
    checksum = hashlib.sha256(payload).hexdigest()
    print(f"  {item['id']}: {filename} ({len(payload)} bytes)")
    return {
        "id": item["id"],
        "name": item["id"],
        "filename": filename,
        "url": url,
        "source": source,
        "description": item["description"],
        "last_update": last_update,
        "version": version,
        "category": item["category"],
        "checksum": checksum,
        "icon_key": item["id"],
    }, release_url


def main() -> int:
    output = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 1:
        output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)

    entries = []
    icons = {}
    for item in CURATED:
        print(f"Fetching {item['id']}...")
        entry, _ = build_entry(item)
        entries.append(entry)
        color = CATEGORY_COLORS.get(item["category"], (0x55, 0x55, 0x55))
        png = create_png(entry["name"][0], color)
        icons[entry["icon_key"]] = (
            "data:image/png;base64," + base64.b64encode(png).decode("ascii")
        )

    catalog = {
        "name": "AniketosHEN Payloads",
        "icons_url": "icons.json",
        "payloads": entries,
    }
    (output / "payloads.json").write_text(
        json.dumps(catalog, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (output / "icons.json").write_text(
        json.dumps({"icons": icons}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote {len(entries)} payloads to {output / 'payloads.json'}")
    print(f"Wrote {len(icons)} icons to {output / 'icons.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
