#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LfoEngine.h"
#include <cmath>

namespace
{
const juce::Colour kBg      = juce::Colour(0xff17191f);
const juce::Colour kPanel   = juce::Colour(0xff10121a);
const juce::Colour kAccent  = juce::Colour(0xff36e0c0);
const juce::Colour kAccentD = juce::Colour(0xff1c8f7d);
const juce::Colour kText    = juce::Colour(0xffdfe4ec);
const juce::Colour kDim     = juce::Colour(0xff7c8494);
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
        setColour(juce::ComboBox::arrowColourId, kAccent);
        setColour(juce::PopupMenu::backgroundColourId, kPanel);
        setColour(juce::PopupMenu::textColourId, kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccentD);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float track = radius * 0.26f;

        // Track arc
        juce::Path bg;
        bg.addCentredArc(centre.x, centre.y, radius - track, radius - track,
                         0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff2b2f3a));
        g.strokePath(bg, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc
        juce::Path fg;
        fg.addCentredArc(centre.x, centre.y, radius - track, radius - track,
                         0.0f, startAngle, angle, true);
        g.setColour(kAccent);
        g.strokePath(fg, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob face
        const float knobR = radius - track * 1.6f;
        g.setColour(kPanel);
        g.fillEllipse(centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour(juce::Colour(0xff2b2f3a));
        g.drawEllipse(centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        // Pointer
        juce::Path p;
        p.addRoundedRectangle(-1.5f, -knobR + 3.0f, 3.0f, knobR * 0.55f, 1.5f);
        g.setColour(kAccent);
        g.fillPath(p, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
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
        g.setColour(juce::Colour(0xff2b2f3a));
        g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.0f);

        auto plot = b.reduced(10.0f);
        const float top = plot.getY(), bottom = plot.getBottom(), h = plot.getHeight();
        const float left = plot.getX(), right = plot.getRight(), w = plot.getWidth();
        const float centreX = plot.getCentreX();

        const int   wave  = static_cast<int>(proc_.getValueTreeState().getRawParameterValue("wave")->load());
        const float depth = proc_.getValueTreeState().getRawParameterValue("amount")->load() / 100.0f;
        const float pw    = proc_.getValueTreeState().getRawParameterValue("pw")->load() / 100.0f;
        const double phase = proc_.getLfoPhase();   // continuous
        const double freq  = juce::jmax(1.0e-4, proc_.getLfoFreqHz());

        // Show roughly four cycles, clamped for very slow/fast rates.
        const double windowSec = juce::jlimit(1.5, 12.0, 4.0 / freq);
        const double pxPerSec  = w / windowSec;

        auto envToY = [&](float env) { return bottom - env * h; };

        // Depth band: the region the gain sweeps through.
        g.setColour(juce::Colour(0x14ffffff));
        g.fillRect(juce::Rectangle<float>(left, envToY(1.0f), w, depth * h));

        // Grid: horizontal mid + full/zero, faint vertical time ticks.
        g.setColour(juce::Colour(0x11ffffff));
        g.drawHorizontalLine(static_cast<int>(envToY(1.0f)), left, right);
        g.drawHorizontalLine(static_cast<int>(envToY(0.5f)), left, right);
        g.drawHorizontalLine(static_cast<int>(envToY(0.0f)), left, right);

        // Waveform path.
        juce::Path curve, fill;
        bool started = false;
        const float step = 1.5f;
        for (float x = left; x <= right; x += step)
        {
            const double dt = (x - centreX) / pxPerSec;
            const double ph = phase + dt * freq;
            const float openness = pulse::waveShape(wave, ph, pw);
            const float env = (1.0f - depth) + depth * openness;
            const float y = envToY(env);
            if (!started) { curve.startNewSubPath(x, y); fill.startNewSubPath(x, bottom); fill.lineTo(x, y); started = true; }
            else          { curve.lineTo(x, y); fill.lineTo(x, y); }
        }
        fill.lineTo(right, bottom);
        fill.closeSubPath();

        g.setColour(kAccent.withAlpha(0.12f));
        g.fillPath(fill);
        g.setColour(kAccent);
        g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));

        // "Now" line at centre.
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawVerticalLine(static_cast<int>(centreX), top, bottom);

        // Current value dot.
        const float envNow = (1.0f - depth) + depth * pulse::waveShape(wave, phase, pw);
        const float yNow = envToY(envNow);
        g.setColour(juce::Colours::white);
        g.fillEllipse(centreX - 4.0f, yNow - 4.0f, 8.0f, 8.0f);
        g.setColour(kAccent);
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

    void paint(juce::Graphics& g) override
    {
        const int   n   = static_cast<int>(pulse::Wave::numWaves);
        const int   sel = static_cast<int>(vts_.getRawParameterValue("wave")->load());
        const float pw  = vts_.getRawParameterValue("pw")->load() / 100.0f;
        const float cellW = (getWidth() - kGap * (n - 1)) / static_cast<float>(n);
        const float hh = (float) getHeight();

        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<float> cell(i * (cellW + kGap), 0.0f, cellW, hh);
            const bool selected = (i == sel);

            // Green-on-black: selected tile gets a green glow + border, others stay dark.
            g.setColour(selected ? kAccent.withAlpha(0.16f) : kPanel);
            g.fillRoundedRectangle(cell, 5.0f);
            g.setColour(selected ? kAccent : juce::Colour(0xff2b2f3a));
            g.drawRoundedRectangle(cell.reduced(0.75f), 5.0f, selected ? 1.5f : 1.0f);

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
                default:                      sample(2.0, 0.5f); break;
            }
            // Bright green when selected, dim green-grey otherwise.
            g.setColour(selected ? kAccent : kDim);
            g.strokePath(path, juce::PathStrokeType(1.7f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int   n   = static_cast<int>(pulse::Wave::numWaves);
        const float cellW = (getWidth() - kGap * (n - 1)) / static_cast<float>(n);
        const int   i = juce::jlimit(0, n - 1, static_cast<int>(e.position.x / (cellW + kGap)));
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
PulseAudioProcessorEditor::PulseAudioProcessorEditor(PulseAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    lnf_ = std::make_unique<PulseLookAndFeel>();
    setLookAndFeel(lnf_.get());

    display_ = std::make_unique<WaveDisplay>(p);
    addAndMakeVisible(*display_);

    auto& vts = processorRef.getValueTreeState();

    // Wave — row of clickable thumbnails
    waveSelector_ = std::make_unique<WaveSelector>(vts);
    addAndMakeVisible(*waveSelector_);

    // Width (square-wave duty cycle)
    widthKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    widthKnob_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    widthKnob_.setTextValueSuffix(" %");
    widthKnob_.setColour(juce::Slider::textBoxTextColourId, kText);
    widthKnob_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(widthKnob_);
    widthAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "pw", widthKnob_);

    // Rate
    rateKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rateKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(rateKnob_);
    rateAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "rate", rateKnob_);

    // Sync
    syncButton_.setClickingTogglesState(true);
    syncButton_.setColour(juce::TextButton::buttonColourId, kPanel);
    syncButton_.setColour(juce::TextButton::buttonOnColourId, kAccentD);
    syncButton_.setColour(juce::TextButton::textColourOffId, kDim);
    syncButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(syncButton_);
    syncAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "sync", syncButton_);

    // Amount
    amountKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amountKnob_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    amountKnob_.setTextValueSuffix(" %");
    amountKnob_.setColour(juce::Slider::textBoxTextColourId, kText);
    amountKnob_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(amountKnob_);
    amountAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "amount", amountKnob_);

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
    setupLabel(syncLabel_,   "SYNC");
    setupLabel(amountLabel_, "AMOUNT");

    rateValue_.setJustificationType(juce::Justification::centred);
    rateValue_.setColour(juce::Label::textColourId, kText);
    rateValue_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    addAndMakeVisible(rateValue_);

    setSize(560, 470);
    startTimerHz(60);
}

PulseAudioProcessorEditor::~PulseAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void PulseAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient grad(kBg, 0, 0, juce::Colour(0xff0d0e12), 0, (float) getHeight(), false);
    g.setGradientFill(grad);
    g.fillAll();

    // Title
    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    g.drawText("PULSE", 20, 14, 200, 26, juce::Justification::left);
    g.setColour(kAccent);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("LFO TREMOLO", 20, 40, 200, 14, juce::Justification::left);
}

void PulseAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(20);
    r.removeFromTop(46);   // title area

    display_->setBounds(r.removeFromTop(168));

    // Wave thumbnail row
    r.removeFromTop(12);
    waveLabel_.setBounds(r.removeFromTop(16));
    waveSelector_->setBounds(r.removeFromTop(54));

    // Knob row: Width | Rate | Sync | Amount
    r.removeFromTop(14);
    auto row = r;
    const int cellW = row.getWidth() / 4;
    auto widthCell  = row.removeFromLeft(cellW);
    auto rateCell   = row.removeFromLeft(cellW);
    auto syncCell   = row.removeFromLeft(cellW);
    auto amountCell = row;

    auto placeLabelTop = [](juce::Rectangle<int> cell, juce::Label& label) {
        label.setBounds(cell.removeFromTop(16));
        return cell;
    };

    { auto c = placeLabelTop(widthCell, widthLabel_);
      widthKnob_.setBounds(c); }

    { auto c = placeLabelTop(rateCell, rateLabel_);
      rateValue_.setBounds(c.removeFromBottom(18));
      rateKnob_.setBounds(c); }

    { auto c = placeLabelTop(syncCell, syncLabel_);
      c.removeFromTop(20);
      syncButton_.setBounds(c.withSizeKeepingCentre(66, 34)); }

    { auto c = placeLabelTop(amountCell, amountLabel_);
      amountKnob_.setBounds(c); }
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
    widthLabel_.setColour(juce::Label::textColourId, isSquare ? kDim : kDim.withAlpha(0.4f));

    // Rate readout: Hz when free, note division when synced.
    const float norm = vts.getRawParameterValue("rate")->load();
    if (vts.getRawParameterValue("sync")->load() >= 0.5f)
        rateValue_.setText(pulse::divisionFromNorm(norm).label, juce::dontSendNotification);
    else
        rateValue_.setText(juce::String(pulse::freeHzFromNorm(norm), 2) + " Hz", juce::dontSendNotification);
}
