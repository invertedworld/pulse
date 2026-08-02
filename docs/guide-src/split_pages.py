#!/usr/bin/env python3
"""Split guide.html into one file per page so each can be screenshotted exactly."""
import pathlib
import re

HERE = pathlib.Path(__file__).parent
html = (HERE / "guide.html").read_text(encoding="utf-8")

head, body = html.split("<body>", 1)
body = body.rsplit("</body>", 1)[0]

# Pages are top-level <div class="page ..."> siblings; split on the opening tag.
parts = re.split(r'(?=<div class="page)', body)
pages = [p for p in parts if p.strip().startswith('<div class="page')]

for i, page in enumerate(pages, 1):
    (HERE / f"pg{i}.html").write_text(
        f"{head}<body>{page}</body></html>", encoding="utf-8")

print(f"{len(pages)} pages")
