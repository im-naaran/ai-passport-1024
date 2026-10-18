#!/usr/bin/env python3
"""Encode shared text vectors as UTF-8 bytes, including intentionally invalid surrogates."""
import json
from pathlib import Path

cases = json.loads(Path("test/fixtures/lightboard/text-vectors.json").read_text())


def literal(value):
    return '"' + ''.join(f"\\{b:03o}" for b in value.encode("utf-8", errors="surrogatepass")) + '"'


lines = ["// Generated from test/fixtures/lightboard/text-vectors.json.",
         "static const struct { const char *input; bool valid; unsigned count; const char *texts[6]; } vectors[] = {"]
for case in cases:
    expected = case["texts"]
    texts = ",".join(literal(s) for s in expected) if expected else "NULL"
    lines.append("    {" + literal(case["input"]) + "," + ("true" if expected else "false") +
                 f",{len(expected or [])}," + "{" + texts + "}},")
lines.append("};")
Path("test/test_lightboard_record/text_vectors.inc").write_text("\n".join(lines) + "\n")
