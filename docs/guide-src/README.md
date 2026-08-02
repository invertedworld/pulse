# User guide source

`../PulseUserGuide.pdf` is generated from these files. CMake compiles that PDF into the plugin
binary (`juce_add_binary_data`), and the `?` button in the UI extracts and opens it — so
regenerating the guide and rebuilding is what ships an updated manual.

## Rebuilding the PDF

```bash
cd docs/guide-src
python3 build_guide.py                     # writes guide.html with screenshots inlined as data URIs
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    --headless --disable-gpu --no-pdf-header-footer --virtual-time-budget=8000 \
    --print-to-pdf=../PulseUserGuide.pdf guide.html
cd ../.. && ./build.sh                     # re-embeds the new PDF
```

Headless Chrome is the renderer because it handles the CSS gradients, `background-clip: text` and
web fonts the theme relies on. `@page { size: A4; margin: 0 }` plus fixed 794×1123px `.page`
blocks means one `<div class="page">` maps to exactly one PDF page.

## Checking the result — render the PDF, not the HTML

**Always verify the generated PDF itself.** Screenshotting the HTML in Chrome is not a substitute:
several CSS features render perfectly on screen and then break during PDF conversion, so an
HTML-only check cannot see them. Two that already bit us:

| CSS | On screen | In the PDF |
| --- | --- | --- |
| `background-clip: text` | gradient-filled letters | Quartz paints the gradient box *over* the text; poppler leaks a band above it |
| `box-shadow` | soft glow | Quartz rasterises it as a hard-edged rectangle — a grey slab around every image |

Both are avoided in the current stylesheet. If you add either back, expect it to look fine in the
browser and wrong in the PDF.

macOS renders PDFs with Quartz, so Preview and Quick Look agree with each other but *not* always
with poppler. Check with both — a defect can show in one and not the other:

```bash
# Quartz (what Preview shows), one PNG per page
pdfseparate ../PulseUserGuide.pdf /tmp/p-%d.pdf
for i in 1 2 3 4 5 6 7; do qlmanage -t -s 1000 -o /tmp /tmp/p-$i.pdf; done

# poppler
pdftoppm -png -r 110 ../PulseUserGuide.pdf /tmp/pop      # needs: brew install poppler
```

Also watch for overflow: `print-to-pdf` gives no warning when content runs past a page, it just
silently clips. Anything below the folio at the bottom of a rendered page is being cut off.
`split_pages.py` writes one `pgN.html` per page if it helps to isolate a page while iterating —
but confirm the fix in the PDF.

## Screenshots

Captured from the standalone at its default window size, cropped to exclude the standalone's own
title bar and the "audio input is muted" banner. Controls sit at their defaults (rate 1.00 Hz)
except where a specific value is the point being illustrated — `shot_sync` is synced to 1/8, and
`shot_env_positive` / `shot_env_negative` use ±80% filter env at a 6 kHz cutoff.

`shot_header.png` is currently unused: it showed the `?` button, which was cut on the grounds that
anyone reading the guide has already found it.
