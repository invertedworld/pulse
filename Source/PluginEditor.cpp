#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LfoEngine.h"
#include "BinaryData.h"
#include <cmath>

namespace
{
// The backdrop is the synthwave sky: deep indigo up top, darkening towards the
// bottom of the window.
const juce::Colour kBgTop   = juce::Colour(0xff2b1150);
const juce::Colour kBgBot   = juce::Colour(0xff120720);
const juce::Colour kPanel   = juce::Colour(0xff0d0518);   // display well, knob faces
const juce::Colour kOutline = juce::Colour(0xff3d2168);   // borders and unlit knob tracks
const juce::Colour kText    = juce::Colour(0xfff2e9ff);
const juce::Colour kDim     = juce::Colour(0xff9b83c4);

// One neon per section, taken straight off the reference: the magenta grid, the
// blue grid, the sun. Amp and Filter both draw curves on the display and cross
// each other constantly, so they sit at opposite ends of the spectrum.
const juce::Colour kShape  = juce::Colour(0xffff3ccf);   // magenta: wave, width, rate
const juce::Colour kShapeD = juce::Colour(0xff8f1a73);   // ...dimmed, for the lit sync button
const juce::Colour kAmp    = juce::Colour(0xff22d3ff);   // electric blue: amplitude
const juce::Colour kFilter = juce::Colour(0xffffd83d);   // yellow: filter env, cutoff, resonance

// Caption strip above each dial. A component's painting is clipped to its own
// bounds, so a dial whose bounds start below its caption gets its glow sliced
// off in a hard line right under the text. Instead each dial's component covers
// the caption strip and the LookAndFeel skips the same amount when placing the
// dial — the strip becomes headroom the glow can spill into, and the caption is
// pushed behind the dial in the z-order so the light washes over it.
constexpr int kCaptionH = 16;

// The halo half of the neon effect.
//
// What reads as banding is the alpha *jump* between neighbouring layers, not
// the layer count itself. So rather than hand-picking an alpha per layer, one
// constant alpha is solved from the target the stack should accumulate to:
// 1 - (1 - a)^layers = kHalo. Adding layers then only ever buys smoothness and
// never changes the brightness, so each caller can trade cost for quality
// freely. Widths ramp quadratically, packing the layers towards the core where
// the light is strongest.
// Straight runs that butt into another line want square ends: a rounded cap
// piles up into a bright blob at the junction, which is exactly what you do not
// want on a grid.
inline void neonGlow(juce::Graphics& g, const juce::Path& p, juce::Colour c,
                     float width, float spread, int layers, float halo = 0.5f,
                     juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded)
{
    const float a = 1.0f - std::pow(1.0f - halo, 1.0f / static_cast<float>(layers));

    g.setColour(c.withMultipliedAlpha(a));
    for (int i = layers; i >= 1; --i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(layers);   // 1 = outermost
        g.strokePath(p, juce::PathStrokeType(width * (1.0f + (spread - 1.0f) * t * t),
                                             juce::PathStrokeType::curved, cap));
    }
}

// Drops the bundled user guide into a temp file and hands it to whatever the
// system uses for PDFs. Rewritten on every click rather than cached, so an
// updated plugin can never open a stale guide left behind by an older build.
void openUserGuide()
{
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("Pulse User Guide.pdf");

    if (f.replaceWithData(BinaryData::PulseUserGuide_pdf,
                          static_cast<size_t>(BinaryData::PulseUserGuide_pdfSize)))
        f.startAsProcess();
}

// Full neon: halo, the tube itself, then its over-exposed centre. Used for
// every glowing line in the UI so they all read as the same material.
inline void neonStroke(juce::Graphics& g, const juce::Path& p, juce::Colour c,
                       float width, int layers = 32, float halo = 0.5f)
{
    neonGlow(g, p, c, width, 6.0f, layers, halo);

    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    g.setColour(c.brighter(0.85f).withMultipliedAlpha(0.85f));
    g.strokePath(p, juce::PathStrokeType(width * 0.38f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
}
}

//==============================================================================
// Rotary knobs and combo/button styling.
class PulseAudioProcessorEditor::PulseLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PulseLookAndFeel()
    {
        setColour(juce::ComboBox::backgroundColourId, kPanel);
        setColour(juce::ComboBox::textColourId, kText);
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2b2f3a));
        setColour(juce::ComboBox::arrowColourId, kShape);
        setColour(juce::PopupMenu::backgroundColourId, kPanel);
        setColour(juce::PopupMenu::textColourId, kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kShapeD);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);

        // Default knob tint is the shape section's, which is what Rate uses.
        // Every other knob sets its own in setupKnob.
        setColour(juce::Slider::rotarySliderFillColourId, kShape);
        setColour(juce::Slider::thumbColourId, kShape);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        // The top strip belongs to the caption; skip it so the dial keeps its
        // size and the space above becomes glow headroom.
        auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
        area.removeFromTop(static_cast<float>(kCaptionH));
        auto bounds = area.reduced(6.0f);

        // Dials sit at half the standard halo: six of them side by side add up
        // to far more light than a single curve does.
        constexpr float kKnobHalo = 0.25f;
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float track = radius * 0.26f;

        // A greyed-out knob (e.g. Width on a non-square wave) keeps its shape
        // but loses the accent colour, so it reads as inactive.
        const float alpha = slider.isEnabled() ? 1.0f : 0.28f;
        const juce::Colour arcColour = slider.findColour(juce::Slider::rotarySliderFillColourId)
                                             .withMultipliedAlpha(alpha);
        const juce::Colour tickColour = slider.findColour(juce::Slider::thumbColourId)
                                              .withMultipliedAlpha(alpha);

        // A bipolar control (Filter Env, -100..100) fills outwards from its zero
        // point rather than from the left end, so "off" reads as an empty ring
        // with the pointer straight up.
        const double lo = slider.getMinimum(), hi = slider.getMaximum();
        const bool bipolar = (lo < 0.0 && hi > 0.0);
        const float origin = bipolar
            ? startAngle + static_cast<float>(-lo / (hi - lo)) * (endAngle - startAngle)
            : startAngle;

        // Knob face first: it is opaque, so drawing it after the arc would clip
        // off the light spilling inwards and flatten the whole effect.
        const float knobR = radius - track * 1.6f;
        g.setColour(kPanel);
        g.fillEllipse(centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour(kOutline);
        g.drawEllipse(centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        // Unlit track
        juce::Path bg;
        bg.addCentredArc(centre.x, centre.y, radius - track, radius - track,
                         0.0f, startAngle, endAngle, true);
        g.setColour(kOutline);
        g.strokePath(bg, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc, lit like neon tubing — its halo now falls across the face
        // and out onto the background. Skipped when it would be degenerate,
        // which would otherwise leave a stray blob under the cap at the origin.
        if (std::abs(angle - origin) > 1.0e-3f)
        {
            juce::Path fg;
            fg.addCentredArc(centre.x, centre.y, radius - track, radius - track,
                             0.0f, origin, angle, true);
            neonStroke(g, fg, arcColour, track, 32, kKnobHalo);
        }

        // Pointer, lit to match.
        juce::Path p;
        p.addRoundedRectangle(-1.5f, -knobR + 3.0f, 3.0f, knobR * 0.55f, 1.5f);
        const auto spin = juce::AffineTransform::rotation(angle).translated(centre.x, centre.y);

        juce::Path spun = p;
        spun.applyTransform(spin);
        neonGlow(g, spun, tickColour, 1.6f, 5.0f, 20, kKnobHalo);

        g.setColour(tickColour);
        g.fillPath(spun);
        // Narrowed about its own centreline for the hot filament.
        g.setColour(tickColour.brighter(0.85f).withMultipliedAlpha(0.85f));
        g.fillPath(p, juce::AffineTransform::scale(0.42f, 1.0f).followedBy(spin));
    }
};

//==============================================================================
// The scrolling waveform. Right = future, centre = now, left = past.
// It draws the actual volume envelope (gain), so Amount and Wave are both
// visible in real time.
class PulseAudioProcessorEditor::WaveDisplay final : public juce::Component
{
public:
    explicit WaveDisplay(PulseAudioProcessor& p) : proc_(p) { setOpaque(false); }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float corner = 8.0f;

        g.setColour(kPanel);
        g.fillRoundedRectangle(b, corner);
        g.setColour(kOutline);
        g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.0f);

        auto plot = b.reduced(10.0f);
        const float top = plot.getY(), bottom = plot.getBottom(), h = plot.getHeight();
        const float left = plot.getX(), right = plot.getRight(), w = plot.getWidth();
        const float centreX = plot.getCentreX();

        const int   wave  = static_cast<int>(proc_.getValueTreeState().getRawParameterValue("wave")->load());
        const float depth = proc_.getValueTreeState().getRawParameterValue("amount")->load() / 100.0f;
        const float pw    = proc_.getValueTreeState().getRawParameterValue("pw")->load() / 100.0f;
        const float filt  = proc_.getValueTreeState().getRawParameterValue("filter")->load() / 100.0f;
        const float cutHz = proc_.getValueTreeState().getRawParameterValue("cutoff")->load();
        const double phase = proc_.getLfoPhase();   // continuous
        const double freq  = juce::jmax(1.0e-4, proc_.getLfoFreqHz());

        // The filter curve appears whenever the filter can colour something.
        const bool showFilter = std::abs(filt) > 1.0e-4f
                             || cutHz < static_cast<float>(pulse::kFiltMaxHz) - 1.0f;

        // Both the envelope and the cutoff map exponentially over the same
        // range, so the plotted height is just their product — no need to go
        // via Hz and back for every pixel.
        const float ceilPos = pulse::filterDisplayPos(cutHz);

        // Show roughly four cycles, clamped for very slow/fast rates.
        const double windowSec = juce::jlimit(1.5, 12.0, 4.0 / freq);
        const double pxPerSec  = w / windowSec;

        auto envToY = [&](float env) { return bottom - env * h; };

        // Depth band: the region the gain sweeps through.
        g.setColour(kAmp.withAlpha(0.07f));
        g.fillRect(juce::Rectangle<float>(left, envToY(1.0f), w, depth * h));

        // The synthwave grid: time ticks every eighth of the window, plus
        // horizontals at zero / half / full. Collected into one path so the
        // whole grid lights in a single pass instead of ten. The colour carries
        // its own low alpha, which neonGlow multiplies through, keeping the grid
        // behind the curves rather than competing with them.
        juce::Path grid;
        for (int i = 1; i < 8; ++i)
        {
            const float gx = std::round(left + w * static_cast<float>(i) / 8.0f);
            grid.startNewSubPath(gx, top);
            grid.lineTo(gx, bottom);
        }
        for (const float level : { 0.0f, 0.5f, 1.0f })
        {
            const float gy = std::round(envToY(level));
            grid.startNewSubPath(left, gy);
            grid.lineTo(right, gy);
        }

        neonGlow(g, grid, kShape.withAlpha(0.5f), 1.0f, 5.0f, 10, 0.5f, juce::PathStrokeType::butt);
        g.setColour(kShape.withAlpha(0.5f));
        g.strokePath(grid, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::butt));

        // Waveform path, plus the filter's cutoff position on the same axis
        // (top = wide open, bottom = fully closed).
        juce::Path curve, fill, filtCurve;
        bool started = false;
        const float step = 1.5f;
        for (float x = left; x <= right; x += step)
        {
            const double dt = (x - centreX) / pxPerSec;
            const double ph = phase + dt * freq;
            const float openness = pulse::waveShape(wave, ph, pw);
            const float env = (1.0f - depth) + depth * openness;
            const float y = envToY(env);
            const float fy = showFilter ? envToY(pulse::filterEnv(openness, filt) * ceilPos) : 0.0f;
            if (!started)
            {
                curve.startNewSubPath(x, y);
                fill.startNewSubPath(x, bottom);
                fill.lineTo(x, y);
                if (showFilter) filtCurve.startNewSubPath(x, fy);
                started = true;
            }
            else
            {
                curve.lineTo(x, y);
                fill.lineTo(x, y);
                if (showFilter) filtCurve.lineTo(x, fy);
            }
        }
        fill.lineTo(right, bottom);
        fill.closeSubPath();

        g.setColour(kAmp.withAlpha(0.10f));
        g.fillPath(fill);

        // Filter first, so the amplitude curve stays the one in front. Fewer
        // halo layers than the knobs: these are long paths redrawn at 60 Hz, and
        // a curve hides stepping far better than a knob's concentric arcs.
        if (showFilter)
            neonStroke(g, filtCurve, kFilter, 1.6f, 14);
        neonStroke(g, curve, kAmp, 2.0f, 14);

        // "Now" line at centre, lit like everything else so it does not read as
        // the one flat line in the display.
        juce::Path now;
        now.startNewSubPath(std::round(centreX), top);
        now.lineTo(std::round(centreX), bottom);
        neonGlow(g, now, juce::Colours::white.withAlpha(0.4f), 1.0f, 5.0f, 10, 0.5f,
                 juce::PathStrokeType::butt);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.strokePath(now, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::butt));

        // Current value dot.
        const float envNow = (1.0f - depth) + depth * pulse::waveShape(wave, phase, pw);
        const float yNow = envToY(envNow);
        g.setColour(juce::Colours::white);
        g.fillEllipse(centreX - 4.0f, yNow - 4.0f, 8.0f, 8.0f);
        g.setColour(kAmp);
        g.fillEllipse(centreX - 2.5f, yNow - 2.5f, 5.0f, 5.0f);
    }

private:
    PulseAudioProcessor& proc_;
};

//==============================================================================
// A row of clickable waveform thumbnails (replaces the wave dropdown). Each
// thumbnail draws its own shape; the selected one is highlighted.
class PulseAudioProcessorEditor::WaveSelector final : public juce::Component
{
public:
    explicit WaveSelector(juce::AudioProcessorValueTreeState& vts) : vts_(vts)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    static constexpr float kGap = 6.0f;

    // Left-to-right display order of the tiles. Independent of the Wave enum /
    // parameter values, so re-ordering here never breaks saved sessions.
    static constexpr int kSlotCount = static_cast<int>(pulse::Wave::numWaves);
    static constexpr int kOrder[kSlotCount] = {
        static_cast<int>(pulse::Wave::square),
        static_cast<int>(pulse::Wave::sampleHold),
        static_cast<int>(pulse::Wave::sine),
        static_cast<int>(pulse::Wave::triangle),
        static_cast<int>(pulse::Wave::sawUp),
        static_cast<int>(pulse::Wave::sawDown),
    };

    static float cellWidth(float totalWidth) noexcept
    {
        return (totalWidth - kGap * static_cast<float>(kSlotCount - 1)) / static_cast<float>(kSlotCount);
    }

    // Centre of a tile, relative to this component's left edge. Used by the
    // editor to line the knobs up under specific icons.
    static float slotCentreX(int slot, float totalWidth) noexcept
    {
        const float cellW = cellWidth(totalWidth);
        return static_cast<float>(slot) * (cellW + kGap) + cellW * 0.5f;
    }

    void paint(juce::Graphics& g) override
    {
        const int   n   = kSlotCount;
        const int   sel = static_cast<int>(vts_.getRawParameterValue("wave")->load());
        const float pw  = vts_.getRawParameterValue("pw")->load() / 100.0f;
        const float cellW = cellWidth(static_cast<float>(getWidth()));
        const float hh = (float) getHeight();

        for (int slot = 0; slot < n; ++slot)
        {
            const int i = kOrder[slot];
            juce::Rectangle<float> cell(static_cast<float>(slot) * (cellW + kGap), 0.0f, cellW, hh);
            const bool selected = (i == sel);

            // The selected tile lights up magenta; the rest stay dark glass.
            g.setColour(selected ? kShape.withAlpha(0.14f) : kPanel);
            g.fillRoundedRectangle(cell, 5.0f);
            if (selected)
            {
                juce::Path border;
                border.addRoundedRectangle(cell.reduced(0.75f), 5.0f);
                neonStroke(g, border, kShape, 1.5f, 18);
            }
            else
            {
                g.setColour(kOutline);
                g.drawRoundedRectangle(cell.reduced(0.75f), 5.0f, 1.0f);
            }

            // Classic textbook glyph per wave.
            auto rr = cell.reduced(cellW * 0.22f, hh * 0.30f);
            const float gl = rr.getX(), gr = rr.getRight();
            const float gt = rr.getY(), gb = rr.getBottom();
            const float gw = rr.getWidth(), gh = rr.getHeight();
            juce::Path path;
            auto sample = [&](double cyc, float width)
            {
                const int steps = 240;
                for (int s = 0; s <= steps; ++s)
                {
                    const double t  = static_cast<double>(s) / steps;
                    const float op  = pulse::waveShape(i, t * cyc, width);
                    const float x   = gl + static_cast<float>(t) * gw;
                    const float y   = gb - op * gh;
                    if (s == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
                }
            };
            switch (static_cast<pulse::Wave>(i))
            {
                // Single cycle. Saw up starts with the slope; saw down starts upright.
                case pulse::Wave::sawUp:      path.startNewSubPath(gl, gb); path.lineTo(gr, gt); path.lineTo(gr, gb); break;
                case pulse::Wave::sawDown:    path.startNewSubPath(gl, gb); path.lineTo(gl, gt); path.lineTo(gr, gb); break;
                case pulse::Wave::square:     sample(2.0, pw);   break;
                case pulse::Wave::sine:       sample(1.5, 0.5f); break;   // 1.5 cycles reads rounder
                case pulse::Wave::sampleHold:
                {
                    // Fixed staircase (icon only) using the full tile height so
                    // every step reads big. Four held levels, first pinned to 40%.
                    static const float lv[4] = { 0.40f, 0.6824f, 0.0333f, 0.3162f };
                    const float shBot = hh * 0.86f, shH = hh * 0.72f;
                    const int st = 240;
                    for (int s = 0; s <= st; ++s)
                    {
                        const double t  = static_cast<double>(s) / st;
                        const int    k  = juce::jmin(3, static_cast<int>(t * 4));
                        const float  x  = gl + static_cast<float>(t) * gw;
                        const float  y  = shBot - lv[k] * shH;
                        if (s == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
                    }
                    break;
                }
                case pulse::Wave::triangle:
                case pulse::Wave::numWaves:   // not a real shape
                default:                      sample(2.0, 0.5f); break;
            }
            // The selected glyph glows; the rest sit back in dim violet-grey.
            if (selected)
            {
                neonGlow(g, path, kShape, 1.7f, 4.0f, 14);
                g.setColour(kShape);
            }
            else
            {
                g.setColour(kDim);
            }
            g.strokePath(path, juce::PathStrokeType(1.7f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int   n   = kSlotCount;
        const float cellW = cellWidth(static_cast<float>(getWidth()));
        const int   slot = juce::jlimit(0, n - 1, static_cast<int>(e.position.x / (cellW + kGap)));
        const int   i = kOrder[slot];
        if (auto* param = vts_.getParameter("wave"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(n > 1 ? static_cast<float>(i) / (n - 1) : 0.0f);
            param->endChangeGesture();
        }
        repaint();
    }

private:
    juce::AudioProcessorValueTreeState& vts_;
};

//==============================================================================
// A small neon "?" in the header that opens the bundled user guide.
class PulseAudioProcessorEditor::HelpButton final : public juce::Button
{
public:
    HelpButton() : juce::Button("Help")
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Open the Pulse user guide");
    }

    void paintButton(juce::Graphics& g, bool hover, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.5f);
        const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
        const auto  c = b.getCentre();

        juce::Path ring;
        ring.addEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        // Brightens rather than changes colour, so it lights up like everything else.
        const float lit = down ? 1.0f : (hover ? 0.9f : 0.6f);
        neonGlow(g, ring, kShape.withMultipliedAlpha(lit), 1.3f, 4.0f, 16, 0.35f);
        g.setColour(kShape.withMultipliedAlpha(lit));
        g.strokePath(ring, juce::PathStrokeType(1.3f));

        g.setColour(kText.withMultipliedAlpha(down ? 1.0f : (hover ? 1.0f : 0.75f)));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText("?", getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
PulseAudioProcessorEditor::PulseAudioProcessorEditor(PulseAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    lnf_ = std::make_unique<PulseLookAndFeel>();
    setLookAndFeel(lnf_.get());

    display_ = std::make_unique<WaveDisplay>(p);
    addAndMakeVisible(*display_);

    helpButton_ = std::make_unique<HelpButton>();
    helpButton_->onClick = [] { openUserGuide(); };
    addAndMakeVisible(*helpButton_);

    auto& vts = processorRef.getValueTreeState();

    // Wave — row of clickable thumbnails
    waveSelector_ = std::make_unique<WaveSelector>(vts);
    addAndMakeVisible(*waveSelector_);

    // Every knob but Rate is the same shape: rotary, value text underneath, and
    // a tint that says which section it belongs to.
    auto setupKnob = [this, &vts](juce::Slider& s, const juce::String& paramID,
                                  const juce::String& suffix, juce::Colour tint,
                                  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
        s.setTextValueSuffix(suffix);
        s.setColour(juce::Slider::textBoxTextColourId, kText);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::rotarySliderFillColourId, tint);
        s.setColour(juce::Slider::thumbColourId, tint);
        addAndMakeVisible(s);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, paramID, s);
    };

    // Width shapes the wave itself, so it takes the shape colour rather than
    // the amplitude one, even though it sits on the top row.
    setupKnob(widthKnob_,  "pw",     " %", kShape, widthAtt_);
    setupKnob(amountKnob_, "amount", " %", kAmp,   amountAtt_);

    // Filter row. Red throughout, matching its curve on the display.
    // Filter Env is bipolar, so its arc fills outwards from 12 o'clock (off).
    setupKnob(filterKnob_, "filter", " %", kFilter, filterAtt_);
    setupKnob(cutoffKnob_, "cutoff", {},   kFilter, cutoffAtt_);   // formats its own Hz/kHz
    setupKnob(resKnob_,    "res",    " %", kFilter, resAtt_);

    // Rate
    rateKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rateKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(rateKnob_);
    rateAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "rate", rateKnob_);

    // Sync
    syncButton_.setClickingTogglesState(true);
    syncButton_.setColour(juce::TextButton::buttonColourId, kPanel);
    syncButton_.setColour(juce::TextButton::buttonOnColourId, kShapeD);
    syncButton_.setColour(juce::TextButton::textColourOffId, kDim);
    syncButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(syncButton_);
    syncAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "sync", syncButton_);

    auto setupLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, kDim);
        l.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(l);
    };
    setupLabel(waveLabel_,   "WAVE");
    setupLabel(widthLabel_,  "WIDTH");
    setupLabel(rateLabel_,   "RATE");
    setupLabel(amountLabel_, "AMPLITUDE");
    setupLabel(filterLabel_, "FILTER");
    setupLabel(cutoffLabel_, "CUTOFF");
    setupLabel(resLabel_,    "RES");

    // Each caption carries its section's colour, so the three groups read at a
    // glance even before you touch anything.
    for (auto* l : { &waveLabel_, &widthLabel_, &rateLabel_ })
        l->setColour(juce::Label::textColourId, kShape.withAlpha(0.75f));
    amountLabel_.setColour(juce::Label::textColourId, kAmp.withAlpha(0.75f));
    for (auto* l : { &filterLabel_, &cutoffLabel_, &resLabel_ })
        l->setColour(juce::Label::textColourId, kFilter.withAlpha(0.75f));

    rateValue_.setJustificationType(juce::Justification::centred);
    rateValue_.setColour(juce::Label::textColourId, kText);
    rateValue_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    addAndMakeVisible(rateValue_);

    syncButton_.setButtonText("SYNC");
    syncButton_.setColour(juce::TextButton::textColourOffId, kDim);

    // Dials overlap their captions (see kCaptionH), so send the captions to the
    // back of the z-order and let the glow wash over them.
    for (auto* l : { &widthLabel_, &rateLabel_, &amountLabel_,
                     &filterLabel_, &cutoffLabel_, &resLabel_ })
        l->toBack();

    // 20 title margin + 46 title + 168 display + 12 + 16 + 54 selector + 14
    // + 132 amplitude row + 16 + 104 filter row + 20 bottom margin.
    setSize(560, 602);
    startTimerHz(60);
}

PulseAudioProcessorEditor::~PulseAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void PulseAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.setGradientFill(juce::ColourGradient(kBgTop, 0, 0, kBgBot, 0, (float) getHeight(), false));
    g.fillAll();

    // Title, drawn as a path so it can be lit like the rest of the neon.
    juce::GlyphArrangement ga;
    ga.addLineOfText(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)), "PULSE", 20.0f, 37.0f);
    juce::Path title;
    ga.createPath(title);

    g.setColour(kShape.withAlpha(0.22f));
    g.strokePath(title, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(kShape.withAlpha(0.45f));
    g.strokePath(title, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Filled like the sun in the reference: yellow at the top, magenta at the
    // bottom — the three section colours meeting in the logo.
    g.setGradientFill(juce::ColourGradient(kFilter, 0.0f, 17.0f, kShape, 0.0f, 38.0f, false));
    g.fillPath(title);
}

void PulseAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(20);

    // Help sits at the far end of the title line, opposite the logo.
    helpButton_->setBounds(r.getRight() - 26, r.getY() + 2, 26, 26);

    r.removeFromTop(46);   // title area

    display_->setBounds(r.removeFromTop(168));

    // Wave thumbnail row
    r.removeFromTop(12);
    waveLabel_.setBounds(r.removeFromTop(16));
    waveSelector_->setBounds(r.removeFromTop(54));

    // Two knob rows of three: amplitude on top, filter beneath. Both use the
    // same three columns, spread between the first and last wave tiles so
    // everything lines up with the selector above.
    r.removeFromTop(14);

    const auto  sel     = waveSelector_->getBounds();
    const float firstCx = WaveSelector::slotCentreX(0, (float) sel.getWidth());
    const float lastCx  = WaveSelector::slotCentreX(WaveSelector::kSlotCount - 1, (float) sel.getWidth());

    constexpr int kColumns = 3;
    constexpr int kCellW   = 96;
    constexpr int kKnobH   = 72;
    constexpr int kTextH   = 16;   // the value text box under each knob
    constexpr int kRowGap  = 16;

    // The Rate column is the tall one: it carries a value readout and the sync
    // button below its knob.
    constexpr int kAmpRowH    = kCaptionH + kKnobH + kTextH + 6 + 22;
    constexpr int kFilterRowH = kCaptionH + kKnobH + kTextH;

    auto column = [&](int index, int rowY, int rowH) {
        const float t  = static_cast<float>(index) / static_cast<float>(kColumns - 1);
        const int   cx = sel.getX() + juce::roundToInt(firstCx + (lastCx - firstCx) * t);
        return juce::Rectangle<int>(cx - kCellW / 2, rowY, kCellW, rowH);
    };

    // The dial's bounds deliberately start at the caption's top rather than
    // below it, so its glow has somewhere to spill. textH is 0 for Rate, which
    // carries its readout as a separate label instead of a slider text box.
    auto placeKnob = [](const juce::Rectangle<int>& col, juce::Label& caption,
                        juce::Slider& knob, int textH)
    {
        caption.setBounds(col.withHeight(kCaptionH));
        knob.setBounds(col.withHeight(kCaptionH + kKnobH + textH));
    };

    // --- Amplitude row ---
    auto ampRow = r.removeFromTop(kAmpRowH);

    placeKnob(column(0, ampRow.getY(), ampRow.getHeight()), widthLabel_,  widthKnob_,  kTextH);
    placeKnob(column(2, ampRow.getY(), ampRow.getHeight()), amountLabel_, amountKnob_, kTextH);

    { auto c = column(1, ampRow.getY(), ampRow.getHeight());
      placeKnob(c, rateLabel_, rateKnob_, 0);
      c.removeFromTop(kCaptionH + kKnobH);
      rateValue_.setBounds(c.removeFromTop(kTextH));
      c.removeFromTop(6);
      syncButton_.setBounds(c.withSizeKeepingCentre(52, 22).withY(c.getY())); }

    // --- Filter row ---
    r.removeFromTop(kRowGap);
    auto filtRow = r.removeFromTop(kFilterRowH);

    struct { juce::Label* label; juce::Slider* knob; } filterCols[kColumns] = {
        { &filterLabel_, &filterKnob_ },
        { &cutoffLabel_, &cutoffKnob_ },
        { &resLabel_,    &resKnob_    },
    };

    for (int i = 0; i < kColumns; ++i)
        placeKnob(column(i, filtRow.getY(), filtRow.getHeight()),
                  *filterCols[i].label, *filterCols[i].knob, kTextH);
}

void PulseAudioProcessorEditor::timerCallback()
{
    display_->repaint();
    waveSelector_->repaint();

    auto& vts = processorRef.getValueTreeState();

    // Width only applies to the square wave — disable it otherwise.
    const int wave = static_cast<int>(vts.getRawParameterValue("wave")->load());
    const bool isSquare = (static_cast<pulse::Wave>(wave) == pulse::Wave::square);
    widthKnob_.setEnabled(isSquare);
    widthLabel_.setColour(juce::Label::textColourId,
                          kShape.withAlpha(isSquare ? 0.75f : 0.3f));
    widthKnob_.setColour(juce::Slider::textBoxTextColourId, isSquare ? kText : kText.withAlpha(0.3f));
    widthLabel_.repaint();

    // Rate readout: Hz when free, note division when synced.
    const float norm = vts.getRawParameterValue("rate")->load();
    if (vts.getRawParameterValue("sync")->load() >= 0.5f)
        rateValue_.setText(pulse::divisionFromNorm(norm).label, juce::dontSendNotification);
    else
        rateValue_.setText(juce::String(pulse::freeHzFromNorm(norm), 2) + " Hz", juce::dontSendNotification);
}
