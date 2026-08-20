from __future__ import annotations

import json
import re
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
HEADER_ROOT = PROJECT_ROOT / "include" / "LuAudio"
OUTPUT_PATH = PROJECT_ROOT / "private" / "out" / "DocIndex.json"

COMMENT_RE = re.compile(r"/\*\*(.*?)\*/", re.DOTALL)
DECLARATION_RE = re.compile(
    r"\b(enum\s+class|enum|struct|class|using)\s+([A-Za-z_]\w*)"
)
METHOD_RE = re.compile(
    r"(?:^|\n)\s*(?:virtual\s+|static\s+)?(?:const\s+)?[\w:<>,*&\s]+\s+"
    r"([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(?:const\s*)?(?:noexcept\s*)?;"
)
FIELD_RE = re.compile(
    r"(?:^|\n)\s*(?:const\s+)?[\w:<>,*&\s]+\s+([A-Za-z_]\w*)\s*(?:=[^;]+)?;"
)
PARAM_RE = re.compile(r"@param\s+(\w+)\s+(.+)")
TAG_RE = re.compile(r"@(summary|details|example|internal)\s*(.*)")


def clean_comment(comment: str) -> list[str]:
    lines = []
    for line in comment.splitlines():
        line = re.sub(r"^\s*\*\s?", "", line).strip()
        if line:
            lines.append(line)
    return lines


def parse_comment(comment: str) -> dict:
    result = {
        "summary": "",
        "details": "",
        "example": "",
        "params": {},
    }
    is_internal = False
    current_tag = None

    for line in clean_comment(comment):
        match = TAG_RE.match(line)
        if match:
            tag, value = match.groups()
            current_tag = tag
            if tag == "internal":
                is_internal = True
            elif tag in {"summary", "details", "example"}:
                result[tag] = value.strip()
            continue

        param_match = PARAM_RE.match(line)
        if param_match:
            name, text = param_match.groups()
            result["params"][name] = text.strip()
            current_tag = None
            continue

        if current_tag in {"summary", "details", "example"}:
            result[current_tag] = f"{result[current_tag]} {line}".strip()

    result["isInternal"] = is_internal
    return result


def declaration_kind(match: re.Match[str]) -> str:
    token = match.group(1)
    if token.startswith("enum"):
        return "enum"
    if token == "using":
        return "alias"
    return token


def make_entry(kind: str, name: str, comment: dict, source: str) -> dict:
    params = [
        {"name": parameter, "text": text}
        for parameter, text in comment["params"].items()
    ]
    return {
        "kind": kind,
        "name": name,
        "source": source,
        "summary": comment["summary"],
        "details": comment["details"],
        "example": comment["example"],
        "params": params,
        "isInternal": bool(comment.get("isInternal")),
    }


def parse_header(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8")
    source = path.relative_to(HEADER_ROOT).as_posix()
    entries = []
    comments = list(COMMENT_RE.finditer(text))

    for match in DECLARATION_RE.finditer(text):
        name = match.group(2)
        if name in {"namespace", "LuAudio"}:
            continue

        comment = {}
        for candidate in reversed(comments):
            if candidate.end() <= match.start():
                comment = parse_comment(candidate.group(1))
                break

        entries.append(make_entry(declaration_kind(match), name, comment, source))

    for match in METHOD_RE.finditer(text):
        name = match.group(1)
        if name in {"if", "for", "while", "switch"}:
            continue
        comment = {}
        for candidate in reversed(comments):
            if candidate.end() <= match.start():
                comment = parse_comment(candidate.group(1))
                break
        entries.append(make_entry("method", name, comment, source))

    return entries


def main() -> None:
    entries = []
    for header in sorted(HEADER_ROOT.rglob("*.h")):
        entries.extend(parse_header(header))

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "format": 1,
        "project": "LuAudio",
        "sourceRoot": "include/LuAudio",
        "entries": entries,
    }
    OUTPUT_PATH.write_text(
        json.dumps(document, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
