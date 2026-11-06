#!/usr/bin/env python3
"""Erzeugt CHANGELOG.md aus inc/firmware.php der Webseite (RELEASES-Array).

Die Webseite ist die einzige Quelle für Versionstexte. Nach jeder neuen Version:
    python3 tools/changelog_from_site.py /pfad/zu/GEschenk/inc/firmware.php > CHANGELOG.md
"""
import re
import subprocess
import sys

REPO = "https://github.com/Pixel-Propeller/FLYGLINJE"


def parse(php: str):
    body = php.split("const RELEASES = [", 1)[1].split("\n];", 1)[0]
    out = []
    for block in re.findall(r"\[\s*'version'.*?'items'\s*=>\s*\[(?:.*?)\],\s*\]", body, re.S):
        get = lambda k: re.search(rf"'{k}'\s*=>\s*'((?:[^'\\]|\\.)*)'", block).group(1).replace("\\'", "'")
        items_src = re.search(r"'items'\s*=>\s*\[(.*?)\],\s*\]$", block, re.S).group(1)
        items = [i.replace("\\'", "'") for i in re.findall(r"'((?:[^'\\]|\\.)*)'", items_src)]
        out.append((get("version"), get("date"), get("name"), items))
    return out


def main():
    php = open(sys.argv[1], encoding="utf-8").read()
    rel = parse(php)
    lines = [
        "# Changelog",
        "",
        "Alle Versionen der FLYGLINJE-Firmware. Jede Version hat einen Spitznamen aus der Fliegerei.",
        "Erzeugt aus der Versionsliste auf [bastelbyra.de/firmware](https://bastelbyra.de/firmware/) "
        "mit `tools/changelog_from_site.py` – bitte dort pflegen, nicht hier.",
        "",
        "## [Unreleased]",
        "",
    ]
    for v, d, n, items in rel:
        lines.append(f"## [{v.lstrip('v')}] – {d} · „{n}“")
        lines.append("")
        lines += [f"- {i}" for i in items]
        lines.append("")
    # Links nur für Versionen, die als Git-Tag existieren (ältere gibt es nur auf der Webseite)
    try:
        tags = set(subprocess.run(["git", "tag"], capture_output=True, text=True).stdout.split())
    except OSError:
        tags = set()
    tags.add(rel[0][0])  # die neueste Version wird gerade getaggt
    lines.append(f"[Unreleased]: {REPO}/compare/{rel[0][0]}...HEAD")
    for v, *_ in rel:
        if v in tags:
            lines.append(f"[{v.lstrip('v')}]: {REPO}/releases/tag/{v}")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
