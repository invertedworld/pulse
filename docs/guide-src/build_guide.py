#!/usr/bin/env python3
"""Build the Pulse user guide HTML with screenshots embedded as data URIs.

Everything is inlined so the file can be handed to headless Chrome without any
relative-path or file-access-origin issues.
"""
import base64
import pathlib

HERE = pathlib.Path(__file__).parent


def img(name, cls="", alt=""):
    data = base64.b64encode((HERE / name).read_bytes()).decode()
    return f'<img class="{cls}" alt="{alt}" src="data:image/png;base64,{data}">'


# --- Palette: lifted straight from the plugin's PluginEditor.cpp -------------
CSS = """
@page { size: A4; margin: 0; }

:root {
  --bg-top:  #2b1150;
  --bg-bot:  #120720;
  --panel:   #0d0518;
  --outline: #3d2168;
  --shape:   #ff3ccf;   /* magenta: wave, width, rate */
  --amp:     #22d3ff;   /* electric blue: amplitude   */
  --filter:  #ffd83d;   /* yellow: filter section     */
  --text:    #f2e9ff;
  --dim:     #9b83c4;
}

* { box-sizing: border-box; }

html, body {
  margin: 0; padding: 0;
  background: var(--bg-bot);
  -webkit-print-color-adjust: exact;
  print-color-adjust: exact;
}

body {
  font-family: "Avenir Next", "Helvetica Neue", Helvetica, sans-serif;
  color: var(--text);
  font-size: 11.5px;
  line-height: 1.62;
}

.page {
  position: relative;
  width: 794px;
  height: 1123px;
  padding: 54px 62px;
  overflow: hidden;
  background: linear-gradient(180deg, var(--bg-top) 0%, var(--bg-bot) 100%);
  page-break-after: always;
}
.page:last-child { page-break-after: auto; }

/* The synthwave grid, echoing the plugin's display. */
.page::before {
  content: "";
  position: absolute; inset: 0;
  background-image:
    repeating-linear-gradient(90deg, rgba(255,60,207,.055) 0 1px, transparent 1px 68px),
    repeating-linear-gradient(0deg,  rgba(255,60,207,.055) 0 1px, transparent 1px 68px);
  pointer-events: none;
}
.page > * { position: relative; }

h1, h2, h3, .kicker, .cover-title, th {
  font-family: Futura, "Avenir Next Demi Bold", "Avenir Next", sans-serif;
}

/* --- Cover ---------------------------------------------------------- */
.cover { display: flex; flex-direction: column; align-items: center; text-align: center; }
/* The wordmark is a pre-rendered image (see title.html). Applying the gradient
   with background-clip:text on live HTML looks right on screen but breaks in
   the PDF — Quartz paints the gradient box over the letters, poppler leaks a
   band above them. */
img.cover-title { width: 420px; margin: 40px 0 0; display: block; }
.cover-sub {
  font-size: 15px; letter-spacing: .40em; color: var(--amp);
  text-transform: uppercase; margin: 6px 0 0;
}
/* Scoped to .hero so it never picks up the wordmark image above it.
   margin-top replaces the spacing the old divider rule used to provide. */
.cover img.hero {
  width: 430px; margin-top: 56px; border-radius: 10px;
}
.cover-foot { margin-top: auto; color: var(--dim); font-size: 11px; letter-spacing: .22em; text-transform: uppercase; }
.cover-ver { color: var(--filter); }

/* --- Headings ------------------------------------------------------- */
.kicker {
  font-size: 10px; letter-spacing: .34em; text-transform: uppercase;
  color: var(--shape); margin: 0 0 6px;
}
h1 {
  font-size: 30px; font-weight: 700; letter-spacing: .045em; margin: 0 0 4px;
  color: var(--text);
}
h1 + .lede { color: var(--dim); margin: 0 0 22px; font-size: 12px; }
h2 {
  font-size: 15px; letter-spacing: .05em; margin: 26px 0 9px;
  color: var(--amp);
}
h2.filter-h { color: var(--filter); }
h3 { font-size: 12px; letter-spacing: .04em; margin: 16px 0 5px; color: var(--text); }

p { margin: 0 0 10px; }
strong { color: #fff; font-weight: 600; }
.mag { color: var(--shape); }
.cy  { color: var(--amp); }
.ye  { color: var(--filter); }

ul { margin: 0 0 10px; padding-left: 17px; }
li { margin-bottom: 4px; }
li::marker { color: var(--shape); }

/* --- Figures --------------------------------------------------------
   No box-shadow anywhere: Quartz (Preview, Quick Look) rasterises CSS
   box-shadow as a hard-edged rectangle when the page is converted to PDF,
   so a soft glow on screen becomes a grey block around every image. Same
   reason the heading text-shadows had to go.                            */
figure { margin: 0 0 14px; }
figure img {
  display: block; width: 100%;
  border: 1px solid var(--outline); border-radius: 8px;
}
figcaption { margin-top: 6px; font-size: 10px; color: var(--dim); letter-spacing: .02em; }
.pair { display: flex; gap: 14px; }
.pair figure { flex: 1; margin-bottom: 10px; }
.inline-shot { display: flex; gap: 18px; align-items: flex-start; }
.inline-shot img {
  width: 132px; border: 1px solid var(--outline); border-radius: 8px;
}
.inline-shot img.wide { width: 250px; }

/* --- Tables --------------------------------------------------------- */
table { width: 100%; border-collapse: collapse; margin: 4px 0 14px; font-size: 10.5px; }
th {
  text-align: left; font-size: 9px; letter-spacing: .2em; text-transform: uppercase;
  color: var(--shape); padding: 0 8px 6px 0; border-bottom: 1px solid var(--outline);
  font-weight: 600;
}
td { padding: 6px 8px 6px 0; border-bottom: 1px solid rgba(61,33,104,.5); vertical-align: top; }
td:first-child { color: #fff; font-weight: 600; white-space: nowrap; }
td.num { color: var(--filter); white-space: nowrap; font-variant-numeric: tabular-nums; }

/* --- Call-out ------------------------------------------------------- */
.note {
  border-left: 2px solid var(--filter);
  padding: 9px 13px; margin: 12px 0;
}
.note .lbl {
  font-size: 9px; letter-spacing: .22em; text-transform: uppercase;
  color: var(--filter); display: block; margin-bottom: 2px;
}
.note p:last-child { margin-bottom: 0; }

.folio {
  position: absolute; left: 62px; right: 62px; bottom: 26px;
  display: flex; justify-content: space-between;
  font-size: 9px; letter-spacing: .2em; text-transform: uppercase; color: var(--dim);
  border-top: 1px solid var(--outline); padding-top: 8px;
}
"""


def folio(n):
    return f'<div class="folio"><span>Pulse &nbsp;·&nbsp; User Guide</span><span>{n}</span></div>'


PAGES = []

# ---------------------------------------------------------------- cover
PAGES.append(f"""
<div class="page cover">
  {img('shot_title.png', cls='cover-title', alt='PULSE')}
  <div class="cover-sub">LFO Tremolo &amp; Filter</div>
  {img('shot_hero.png', cls='hero', alt='The Pulse plugin interface')}
  <div class="cover-foot">Version 1.2.1 &nbsp;·&nbsp; <span class="cover-ver">AU &nbsp;VST3 &nbsp;Standalone</span></div>
</div>
""")

# ---------------------------------------------------- overview + install
PAGES.append(f"""
<div class="page">
  <div class="kicker">01 — Getting started</div>
  <h1>What Pulse does</h1>
  <p class="lede">A shape-selectable LFO that chops your signal and sweeps a filter, free-running or locked to the host.</p>

  <p>Pulse runs one low-frequency oscillator and uses it for two jobs at once. It modulates
  <strong>amplitude</strong> — the classic tremolo or rhythmic gate — and it sweeps the cutoff of a
  <strong>resonant low-pass filter</strong>. Both are driven by the same wave, so they always move in
  step with each other and with the beat.</p>

  <p>The LFO either free-runs in hertz or locks to the host tempo and transport. When locked, its phase
  is pinned to the playhead, so a 1/8-note gate lands on the eighth notes no matter where you start
  playback.</p>

  <h2>Installing</h2>
  <p>Run the installer, <span class="ye">Pulse-1.2.1.pkg</span>. At the destination step, choose
  whether to install for <strong>everyone who uses this Mac</strong> or <strong>just for you</strong>.
  Installing for everyone asks for an administrator password; installing for yourself does not, and
  keeps everything inside your home folder.</p>

  <table>
    <tr><th>Format</th><th>Everyone</th><th>Just me</th></tr>
    <tr><td>Audio Unit</td><td class="num">/Library/Audio/Plug-Ins/Components</td><td class="num">~/Library/Audio/Plug-Ins/Components</td></tr>
    <tr><td>VST3</td><td class="num">/Library/Audio/Plug-Ins/VST3</td><td class="num">~/Library/Audio/Plug-Ins/VST3</td></tr>
    <tr><td>Standalone</td><td class="num">/Applications</td><td class="num">~/Applications</td></tr>
  </table>

  <p>You can also choose which of the three to install. All are universal binaries, running natively
  on both Apple Silicon and Intel Macs. Restart your DAW afterwards so it rescans for new plug-ins.</p>

  <h2>Reading the interface</h2>
  <p>Colour tells you which section a control belongs to:</p>
  <ul>
    <li><span class="mag"><strong>Magenta</strong></span> — the shape of the modulation: wave, width, rate.</li>
    <li><span class="cy"><strong>Electric blue</strong></span> — amplitude, and its curve in the display.</li>
    <li><span class="ye"><strong>Yellow</strong></span> — the filter, and its curve in the display.</li>
  </ul>
  <p>Every knob responds to click-and-drag, and to scrolling. Double-click a knob to return it to its
  default. The value readout under each knob is editable — click it and type an exact figure.</p>

  {folio(2)}
</div>
""")

# ------------------------------------------------------------- display
PAGES.append(f"""
<div class="page">
  <div class="kicker">02 — The display</div>
  <h1>Watching the modulation</h1>
  <p class="lede">Time runs left to right. The centre line is now.</p>

  <figure>
    {img('shot_display_basic.png', alt='The scrolling display showing the amplitude curve')}
    <figcaption>The amplitude envelope, drawn in electric blue. Left of the centre line is what has
    already played; right of it is what is about to.</figcaption>
  </figure>

  <p>The display does not show your audio — it shows the <strong>modulation being applied to it</strong>.
  The vertical axis is how open the signal is: the top of the plot is fully open, the bottom is fully
  closed. The white dot on the centre line marks the value being applied at this instant.</p>

  <p>Because the curve is generated from exactly the same maths the audio engine uses, what you see is
  what you hear — including the way the wave will look several cycles from now.</p>

  <h2>The second curve</h2>
  <p>As soon as the filter is doing something, a <span class="ye">yellow curve</span> joins the blue one.
  It plots the filter's cutoff on the same vertical axis: high on the plot means the filter is open and
  bright, low means it is closed and dark.</p>

  <figure>
    {img('shot_display_curve.png', alt='Display showing amplitude and filter curves together')}
    <figcaption>Both curves at once. The yellow filter line rises and falls with the blue amplitude
    line — you can see the filter opening on every swell.</figcaption>
  </figure>

  <div class="note">
    <span class="lbl">Tip</span>
    <p>If the yellow line is missing entirely, the filter is fully bypassed: the envelope is at zero
    <em>and</em> cutoff is wide open. In that state Pulse passes the signal through untouched.</p>
  </div>

  {folio(3)}
</div>
""")

# ----------------------------------------------------------- waveforms
PAGES.append(f"""
<div class="page">
  <div class="kicker">03 — Wave</div>
  <h1>Choosing a shape</h1>
  <p class="lede">Six shapes, from smooth swells to hard gating to random steps.</p>

  <figure>
    {img('shot_waves.png', alt='The six waveform buttons')}
    <figcaption>Click a tile to select it. The lit tile is the active shape.</figcaption>
  </figure>

  <table>
    <tr><th>Shape</th><th>Character</th></tr>
    <tr><td>Square</td><td>Hard on/off gating. The only shape that uses <span class="mag">Width</span>.
      Edges are smoothed by about a millisecond so they click rather than crack.</td></tr>
    <tr><td>S&amp;H</td><td>Sample &amp; hold — a new random level each cycle, held flat. Stepped,
      unpredictable movement. The sequence is fixed, so it plays back identically every time.</td></tr>
    <tr><td>Sine</td><td>Smooth, symmetrical swelling. The classic amp tremolo.</td></tr>
    <tr><td>Triangle</td><td>Linear up and down. Similar to sine but with a firmer turn at the peaks.</td></tr>
    <tr><td>Saw Up</td><td>Ramps open, then snaps shut. Reverse-swell, backwards-tape feel.</td></tr>
    <tr><td>Saw Down</td><td>Snaps open, then decays away. Plucked and percussive.</td></tr>
  </table>

  <h2>Width</h2>
  <p>Width sets the duty cycle of the square wave — the proportion of each cycle spent open. At 50% you
  get an even on/off chop. Wind it down for short stabs, up for brief gaps in an otherwise open signal.</p>
  <p>It has no effect on the other five shapes, and greys out to tell you so.</p>

  <div class="note">
    <span class="lbl">Note</span>
    <p>Sample &amp; hold changes level once per cycle rather than continuously, so with it selected
    <span class="mag">Rate</span> controls how often a new random value is chosen.</p>
  </div>

  {folio(4)}
</div>
""")

# ---------------------------------------------------------- amplitude
PAGES.append(f"""
<div class="page">
  <div class="kicker">04 — Amplitude</div>
  <h1>Rate and depth</h1>
  <p class="lede">How fast the wave runs, and how hard it bites.</p>

  <figure>
    {img('shot_amp.png', alt='The amplitude row: Width, Rate, Sync and Amplitude')}
  </figure>

  <h2>Rate</h2>
  <p>In free mode, Rate sweeps from <span class="ye">0.05 Hz</span> — one cycle every twenty seconds —
  up to <span class="ye">20 Hz</span>, well into audible-flutter territory. The scale is exponential, so
  the slow end gets as much of the knob's travel as the fast end.</p>

  <div class="inline-shot">
    <div>
      <h3>Sync</h3>
      <p>Engage <span class="mag">SYNC</span> and Rate switches from hertz to note divisions, running
      from <span class="ye">1/1</span> down to <span class="ye">1/32</span> and taking in dotted and
      triplet values on the way. The readout under the knob shows the current division.</p>
      <p>Synced, the LFO locks its phase to the host playhead, so cycles land on the grid wherever you
      drop the cursor. If the transport is stopped, it free-runs at the equivalent tempo rather than
      freezing.</p>
    </div>
    {img('shot_sync.png', alt='The Rate knob with Sync engaged, reading 1/8')}
  </div>

  <h2>Amplitude</h2>
  <p>Amplitude is the depth of the tremolo — how much of the wave's travel is applied to the signal.</p>
  <ul>
    <li>At <strong>0%</strong> the signal passes untouched, whatever the wave is doing.</li>
    <li>At <strong>50%</strong> the level swings between full and half.</li>
    <li>At <strong>100%</strong> the troughs reach silence. With a square wave, that is a hard gate.</li>
  </ul>
  <p>The shaded band behind the curve in the display shows the range the level is sweeping through, so
  you can see the depth as well as hear it.</p>

  {folio(5)}
</div>
""")

# ------------------------------------------------------------- filter
PAGES.append(f"""
<div class="page">
  <div class="kicker">05 — Filter</div>
  <h1>Sweeping the cutoff</h1>
  <p class="lede">A resonant 12&nbsp;dB/octave low-pass, driven by the same LFO.</p>

  <figure>
    {img('shot_filter.png', alt='The filter row: Filter envelope, Cutoff and Resonance')}
  </figure>

  <h2 class="filter-h">Filter — the envelope amount</h2>
  <p>This control is <strong>bipolar</strong>. It sits at zero straight up, and its ring fills outwards
  from twelve o'clock rather than from the left, so "off" reads as an empty ring at a glance.</p>

  <div class="pair">
    <figure>
      {img('shot_env_positive.png', alt='Filter envelope positive: yellow curve follows the blue')}
      <figcaption><strong>Positive.</strong> The filter opens with the wave — bright at the peaks,
      dark in the troughs. The yellow curve tracks the blue one.</figcaption>
    </figure>
    <figure>
      {img('shot_env_negative.png', alt='Filter envelope negative: yellow curve mirrors the blue')}
      <figcaption><strong>Negative.</strong> Inverted — the peak of the wave <em>closes</em> the filter.
      The yellow curve mirrors the blue one.</figcaption>
    </figure>
  </div>

  <h2 class="filter-h">Cutoff and Resonance</h2>
  <p><strong>Cutoff</strong> is the ceiling the envelope sweeps down from, adjustable between
  <span class="ye">80 Hz</span> and <span class="ye">20 kHz</span>. At full travel the filter is wide
  open; lower it and the whole sweep moves down with it. With the envelope at zero it simply acts as a
  static low-pass, which is a useful way to tame a source before the tremolo hits it.</p>

  <p><strong>Resonance</strong> lifts a peak at the corner frequency, from flat at 0% to a sharp, vocal
  squelch at 100%. Combined with a slow envelope it gives you an auto-wah; with a fast one, a synth-like
  bubble.</p>

  <h3>Cutoff on its own</h3>
  <div class="inline-shot">
    <div>
      <p>With <span class="ye">Filter</span> at zero, nothing sweeps the cutoff and the yellow line in
      the display flattens out. What you have left is a plain static low-pass — a useful way to tame a
      bright source before the tremolo reaches it.</p>
    </div>
    {img('shot_display_static.png', cls='wide', alt='Display showing a flat yellow filter line')}
  </div>

  <div class="note">
    <span class="lbl">Watch your levels</span>
    <p>High resonance adds considerable gain around the cutoff — up to about 20&nbsp;dB at the extreme.
    On bass-heavy material, pull the source down before pushing Resonance up.</p>
  </div>

  {folio(6)}
</div>
""")

# ---------------------------------------------------------- reference
PAGES.append(f"""
<div class="page">
  <div class="kicker">06 — Reference</div>
  <h1>Every parameter</h1>
  <p class="lede">Ranges and defaults. All eight are automatable from the host.</p>

  <table>
    <tr><th>Control</th><th>Range</th><th>Default</th></tr>
    <tr><td>Wave</td><td>Triangle · Saw Up · Saw Down · Square · Sine · S&amp;H</td><td class="num">Sine</td></tr>
    <tr><td>Rate (free)</td><td>0.05 – 20 Hz, exponential</td><td class="num">1.00 Hz</td></tr>
    <tr><td>Rate (synced)</td><td>1/1 · 1/2. · 1/2 · 1/4. · 1/2T · 1/4 · 1/8. · 1/4T · 1/8 · 1/16. · 1/8T · 1/16 · 1/16T · 1/32</td><td class="num">1/4</td></tr>
    <tr><td>Host Sync</td><td>Off / On</td><td class="num">Off</td></tr>
    <tr><td>Width</td><td>1 – 99 % (square wave only)</td><td class="num">50 %</td></tr>
    <tr><td>Amplitude</td><td>0 – 100 %</td><td class="num">80 %</td></tr>
    <tr><td>Filter</td><td>−100 – +100 % (0 = off)</td><td class="num">0 %</td></tr>
    <tr><td>Cutoff</td><td>80 Hz – 20 kHz</td><td class="num">20 kHz</td></tr>
    <tr><td>Resonance</td><td>0 – 100 % (Q 0.7 – 10)</td><td class="num">0 %</td></tr>
  </table>

  <h2>Starting points</h2>

  <h3>Classic amp tremolo</h3>
  <p>Sine · free · 4–6 Hz · Amplitude 40% · Filter 0%. Gentle, continuous swelling. Take Amplitude down
  to 20% for something that sits under a vocal without drawing attention.</p>

  <h3>Rhythmic gate</h3>
  <p>Square · sync 1/8 or 1/16 · Amplitude 100% · Width 50%. A hard chop locked to the grid. Narrow
  Width to around 25% for shorter, more percussive stabs.</p>

  <h3>Auto-wah</h3>
  <p>Sine · sync 1/2 · Amplitude 0% · Filter +70% · Cutoff 6 kHz · Resonance 60%. Amplitude at zero
  leaves the level alone, so the filter sweep is all you hear.</p>

  <h3>Pumping</h3>
  <p>Saw Up · sync 1/4 · Amplitude 70% · Filter +40% · Cutoff 10 kHz. Ducks on the beat and recovers
  across it, brightening as it returns — the sidechain-compressor trick without a sidechain.</p>

  <h3>Broken machine</h3>
  <p>S&amp;H · sync 1/16 · Amplitude 60% · Filter −80% · Resonance 75%. Random levels with the filter
  slamming shut on the loud steps. Chaotic, and different in feel every bar.</p>

  {folio(7)}
</div>
""")

html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Pulse — User Guide</title>
<style>{CSS}</style></head><body>
{''.join(PAGES)}
</body></html>"""

out = HERE / "guide.html"
out.write_text(html, encoding="utf-8")
print(f"wrote {out} ({len(html)/1024:.0f} KB)")
