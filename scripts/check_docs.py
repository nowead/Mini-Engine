"""Check local file/image links in maintained entry-point documentation.

No network access. Historical plans/archives and heading anchors are excluded.
Run from any working directory: python scripts/check_docs.py
"""

from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_DOCS = (
    "README.md",
    "docs/README.md",
    "docs/SUMMARY.md",
    "docs/ARCHITECTURE.md",
    "docs/ASSETS.md",
    "docs/VALIDATION.md",
    "docs/guides/BUILD_GUIDE.md",
    "docs/guides/CODE_MAP.md",
    "docs/current/README.md",
    "docs/current/medical-volume/README.md",
    "docs/current/medical-volume/VIEWERS.md",
    "tests/README.md",
)


def main():
    failures = []
    checked = 0
    for name in PUBLIC_DOCS:
        source = ROOT / name
        if not source.is_file():
            failures.append(f"{name}: missing document")
            continue
        content = source.read_text(encoding="utf-8")
        content = re.sub(r"^```[^\n]*\n.*?^```[^\n]*$", "", content, flags=re.M | re.S)
        targets = re.findall(r"!?\[[^\]\n]*\]\(([^\s)]+)(?:\s+\"[^\"]*\")?\)", content)
        targets += re.findall(r'(?:src|href)=[\"\']([^\"\']+)[\"\']', content)
        for target in targets:
            target = target.strip("<>")
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            path = unquote(parsed.path)
            resolved = (ROOT / path.lstrip("/") if path.startswith("/") else source.parent / path)
            checked += 1
            if not resolved.exists():
                failures.append(f"{name}: missing target {target}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"Checked {checked} local links in {len(PUBLIC_DOCS)} public documents.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
