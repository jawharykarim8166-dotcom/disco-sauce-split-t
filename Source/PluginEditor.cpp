#include "PluginEditor.h"
#include <BinaryData.h>

using juce::Colour;
using juce::Rectangle;
using juce::Justification;

static const Colour kCyan   (0xff69f6ff);
static const Colour kDimCy  (0xff2fb0bd);
static const Colour kGold   (0xfff6d365);
static const Colour kStem[3] = { Colour (0xffff8fd0), Colour (0xfff6d365), Colour (0xff8dff8a) };

static juce::String pcName (int pc)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return (pc >= 0 && pc < 12) ? juce::String (n[pc]) : juce::String ("--");
}

SplitEditor::SplitEditor (SplitProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    bg = juce::ImageCache::getFromMemory (BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1000.0 / 985.0);
    setResizeLimits (500, 492, 1500, 1477);
    setSize (800, 788);
    startTimerHz (30);
}

SplitEditor::~SplitEditor() { stopTimer(); }

void SplitEditor::resized()
{
    if (bg.isValid() && getWidth() > 0)
        scaledBg = bg.rescaled (getWidth(), getHeight(), juce::Graphics::highResamplingQuality);
}

void SplitEditor::timerCallback() { phase += 0.07f; repaint(); }

//==============================================================================
void SplitEditor::text (juce::Graphics& g, const juce::String& s, Rectangle<float> r, float size, Colour c,
                        Justification j, float kern)
{
    juce::Font f ("Consolas", size, juce::Font::bold);
    f.setExtraKerningFactor (kern);
    g.setFont (f);
    g.setColour (c);
    g.drawText (s, r, j, false);
}

void SplitEditor::paint (juce::Graphics& g)
{
    if (scaledBg.isValid()) g.drawImageAt (scaledBg, 0, 0);
    else g.fillAll (juce::Colours::black);

    g.addTransform (juce::AffineTransform::scale (scale()));

    // name plates
    text (g, "SPLIT'T", { 430.f, 380.f, 172.f, 40.f }, 20.f, kGold, Justification::centred, 0.12f);
    text (g, "PRESS TO SPLIT", { 586.f, 891.f, 164.f, 46.f }, 14.f, kGold, Justification::centred, 0.05f);
    text (g, "LEVEL", { 832.f, 893.f, 146.f, 44.f }, 16.f, Colour (0xffe8ecf8), Justification::centred, 0.1f);

    drawLcd (g);
    drawTiles (g);
    drawPress (g);

    // level slider thumb
    {
        const float ty = thumbY();
        Rectangle<float> r (866.f, ty - 15.f, 64.f, 30.f);
        g.setColour (kGold.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (5.f), 20.f);
        g.setGradientFill (juce::ColourGradient (Colour (0xfffff6c0), 0.f, r.getY(), Colour (0xff9b6c10), 0.f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 15.f);
        g.setColour (Colour (0xfffff4c4));
        g.drawRoundedRectangle (r, 15.f, 3.f);
        if (draggingThumb)
        {
            const float db = (proc.getStemLevel() - 0.8f) * 30.f;
            text (g, juce::String (db, 1) + " dB", { 770.f, ty - 12.f, 90.f, 24.f }, 13.f, juce::Colours::white, Justification::centredRight);
        }
    }

    if (dropHover)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (36.f, 425.f, 600.f, 267.f, 26.f);
        text (g, "DROP AUDIO FILE TO LOAD", { 36.f, 425.f, 600.f, 267.f }, 26.f, kGold, Justification::centred);
    }
}

//==============================================================================
void SplitEditor::drawLcd (juce::Graphics& g)
{
    const int st = proc.getState();
    const float prog = proc.getProgress();
    const SplitResult res = proc.getResult();
    const bool done = (st == 2);
    auto& rnd = juce::Random::getSystemRandom();
    const float ox = 44.f, oy = 433.f;   // glass origin

    text (g, "BPM",   { ox + 16.f,  oy + 11.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    text (g, "KEY",   { ox + 216.f, oy + 11.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    text (g, "SCALE", { ox + 376.f, oy + 11.f, 100.f, 14.f }, 10.f, kDimCy, Justification::left, 0.3f);
    text (g, "CONF",  { ox + 492.f, oy + 11.f, 76.f, 14.f },  10.f, kDimCy, Justification::right, 0.3f);

    juce::String bpm = "---.-", key = "--", scale = "------", conf = "--%";
    if (st == 1)
    {
        bpm = juce::String (60.f + rnd.nextFloat() * 130.f, 1);
        key = pcName (rnd.nextInt (12));
        scale = "......";
        conf = "..";
    }
    else if (done)
    {
        bpm = res.bpm > 0.0 ? juce::String (res.bpm, 1) : juce::String ("---.-");
        key = pcName (res.keyRoot);
        scale = res.keyRoot < 0 ? juce::String ("------") : (res.minor ? "MINOR" : "MAJOR");
        conf = juce::String (juce::roundToInt (juce::jmin (res.bpmConf, res.keyConf))) + "%";
    }

    auto glow = [&] (const juce::String& s, Rectangle<float> r, float size, Justification j)
    {
        text (g, s, r.translated (0.f, 1.5f), size, kCyan.withAlpha (0.25f), j);
        text (g, s, r, size, kCyan, j);
    };
    glow (bpm,   { ox + 16.f,  oy + 22.f, 200.f, 60.f }, 54.f, Justification::left);
    glow (key,   { ox + 216.f, oy + 28.f, 150.f, 52.f }, 44.f, Justification::left);
    glow (scale, { ox + 376.f, oy + 44.f, 120.f, 30.f }, 24.f, Justification::left);
    glow (conf,  { ox + 492.f, oy + 42.f, 76.f, 28.f },  20.f, Justification::right);

    // waveform area
    const float wx = ox + 16.f, wy = oy + 96.f;
    if (done && res.peaks[1].size() == 142)
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto& pk = res.peaks[(size_t) k + 1];
            const float cy = wy + 14.f + 28.f * (float) k;
            g.setColour (kStem[k]);
            for (int i = 0; i < 142; ++i)
            {
                const float h = 2.f + pk[(size_t) i] * 24.f;
                g.fillRect (wx + (float) i * 4.f, cy - h * 0.5f, 2.f, h);
            }
        }
    }
    else
    {
        g.setColour (kCyan);
        const float amp = (st == 1) ? 1.f : 0.75f;
        for (int i = 0; i < 142; ++i)
        {
            const float x = (float) i * 4.f;
            const float a = std::abs (std::sin (x * 0.03f + phase) * std::sin (x * 0.011f - phase * 0.7f)) * amp;
            const float h = 2.f + a * 76.f;
            g.fillRect (wx + x, wy + 42.f - h * 0.5f, 2.f, h);
        }
    }

    // message line + progress bar + hint
    juce::String msg = proc.getStatus();
    if (st == 0 && msg.isEmpty()) msg = "PRESS SPLIT TO ANALYZE AND SPLIT YOUR TRACK";
    text (g, msg, { ox + 16.f, oy + 188.f, 560.f, 16.f }, 11.f, st == 3 ? Colour (0xffff9aa8) : kCyan, Justification::left, 0.08f);

    const Rectangle<float> bar (ox + 16.f, oy + 216.f, 552.f, 7.f);
    g.setColour (Colour (0xff0b2a33));
    g.fillRoundedRectangle (bar, 4.f);
    const float shown = done ? 1.f : (st == 1 ? prog : 0.f);
    if (shown > 0.f)
    {
        g.setGradientFill (juce::ColourGradient (kCyan, bar.getX(), 0.f, Colour (0xff9dff6b), bar.getRight(), 0.f, false));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * shown), 4.f);
    }

    juce::String hint;
    if (done && res.keyRoot >= 0)
        hint = "ALSO READS AS " + pcName (res.relRoot) + (res.minor ? " MAJOR" : " MINOR")
             + "   BPM " + juce::String (juce::roundToInt (res.bpmConf)) + "%  KEY " + juce::String (juce::roundToInt (res.keyConf))
             + "%   (CLICK BPM TO x2 or /2)";
    else
        hint = proc.getInputInfo();
    text (g, hint, { ox + 16.f, oy + 228.f, 560.f, 14.f }, 9.f, kDimCy, Justification::left, 0.1f);
}

//==============================================================================
void SplitEditor::drawTiles (juce::Graphics& g)
{
    const bool done = (proc.getState() == 2);
    const SplitResult res = proc.getResult();
    static const char* names[3] = { "MELODY", "DRUMS & PERC", "BASS" };
    static const char* letters[3] = { "M", "D", "B" };

    for (int i = 0; i < 3; ++i)
    {
        const Rectangle<float> r = tileRect (i);
        const float a = done ? 1.f : 0.5f;
        const Colour c = kStem[i];

        if (done)
            for (int k = 3; k >= 1; --k)
            {
                g.setColour (c.withAlpha (0.07f * (float) (4 - k)));
                g.drawRoundedRectangle (r.expanded ((float) k * 2.f), 16.f + (float) k, 2.f);
            }
        g.setGradientFill (juce::ColourGradient (Colour (0xff1c1c26), 0.f, r.getY(), Colour (0xff08080d), 0.f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 16.f);
        g.setColour (done ? c : Colour (0xff4a5062));
        g.drawRoundedRectangle (r.reduced (1.5f), 16.f, 3.f);

        const Rectangle<float> led (r.getX() + 14.f, r.getY() + 14.f, 38.f, 38.f);
        g.setColour (c.withAlpha (a));
        g.fillRoundedRectangle (led, 11.f);
        text (g, letters[i], led, 22.f, Colour (0xff111111), Justification::centred);

        text (g, names[i], { r.getX() + 64.f, r.getY() + 12.f, 200.f, 22.f }, 15.f, juce::Colours::white.withAlpha (a), Justification::left, 0.12f);
        juce::String sub = done ? res.files[i].getFileName() : juce::String ("WAITING FOR SPLIT");
        text (g, sub, { r.getX() + 64.f, r.getY() + 36.f, 205.f, 14.f }, 9.f, Colour (0xff8d96ad).withAlpha (a), Justification::left);

        // mini waveform
        const float wx = r.getX() + 272.f, cy = r.getY() + 33.f;
        g.setColour (c.withAlpha (a));
        if (done && res.peaks[(size_t) i + 1].size() == 142)
        {
            const auto& pk = res.peaks[(size_t) i + 1];
            for (int b = 0; b < 43; ++b)
            {
                float m = 0.f;
                for (int j = 0; j < 3; ++j) m = juce::jmax (m, pk[(size_t) (b * 3 + j)]);
                const float h = 2.f + m * 34.f;
                g.fillRect (wx + (float) b * 3.f, cy - h * 0.5f, 2.f, h);
            }
        }
        else
        {
            g.setColour (Colour (0xff4a5062));
            for (int b = 0; b < 43; ++b) g.fillRect (wx + (float) b * 3.f, cy - 1.f, 2.f, 2.f);
        }

        // grip
        const float gx = r.getRight() - 62.f;
        g.setColour (c.withAlpha (a));
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (gx + 20.f + (float) col * 7.f, r.getY() + 8.f + (float) row * 6.f, 3.5f, 3.5f);
        text (g, "DRAG TO", { gx - 4.f, r.getY() + 28.f, 62.f, 12.f }, 9.f, c.withAlpha (a), Justification::centred);
        text (g, "PLAYLIST", { gx - 4.f, r.getY() + 40.f, 62.f, 12.f }, 9.f, c.withAlpha (a), Justification::centred);
    }
}

//==============================================================================
void SplitEditor::drawPress (juce::Graphics& g)
{
    const int st = proc.getState();
    const float p = (st == 1) ? juce::jlimit (0.f, 1.f, proc.getProgress()) : 1.f;

    // LED ring dims where the progress has not arrived yet
    if (p < 1.f)
    {
        const float a0 = juce::degreesToRadians (-135.f + p * 270.f), a1 = juce::degreesToRadians (135.f);
        juce::Path arc;
        arc.addCentredArc (668.f, 790.f, 94.f, 94.f, 0.f, a0, a1, true);
        g.setColour (Colour (0xd006060e));
        g.strokePath (arc, juce::PathStrokeType (24.f));
    }

    if (pressed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (583.f, 727.f, 140.f, 140.f);
    }

    float alpha = 1.f;
    if (st == 1) alpha = 0.65f + 0.35f * std::sin (phase * 3.f);
    text (g, "SPLIT", { 583.f, 822.f + (pressed ? 3.f : 0.f), 140.f, 20.f }, 15.f, kGold.withAlpha (alpha), Justification::centred, 0.15f);
}

//==============================================================================
int SplitEditor::tileAt (juce::Point<float> p) const
{
    for (int i = 0; i < 3; ++i)
        if (tileRect (i).contains (p)) return i;
    return -1;
}

void SplitEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toDesign (e);
    dragStarted = false;
    dragTile = -1;

    if (p.getDistanceFrom ({ 668.f, 793.f }) < 100.f)
    {
        pressed = true;
    }
    else if (Rectangle<float> (850.f, 425.f, 96.f, 430.f).contains (p))
    {
        draggingThumb = true;
        proc.setStemLevel (1.f - (p.y - 440.f) / 380.f);
    }
    else if (Rectangle<float> (60.f, 450.f, 200.f, 70.f).contains (p))
    {
        proc.halveDoubleBpm();
    }
    else
    {
        const int t = tileAt (p);
        if (t >= 0 && proc.getState() == 2) dragTile = t;
    }
    repaint();
}

void SplitEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = toDesign (e);
    if (draggingThumb)
    {
        proc.setStemLevel (1.f - (p.y - 440.f) / 380.f);
    }
    else if (dragTile >= 0 && ! dragStarted && e.getDistanceFromDragStart() > 8)
    {
        const juce::File f = proc.getResult().files[dragTile];
        if (f.existsAsFile())
        {
            dragStarted = true;
            juce::StringArray files;
            files.add (f.getFullPathName());
            juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
            dragTile = -1;
        }
    }
}

void SplitEditor::mouseUp (const juce::MouseEvent& e)
{
    const auto p = toDesign (e);
    if (pressed)
    {
        pressed = false;
        if (p.getDistanceFrom ({ 668.f, 793.f }) < 100.f) proc.startSplit();
    }
    draggingThumb = false;
    dragTile = -1;
    repaint();
}

void SplitEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int t = tileAt (toDesign (e));
    if (t >= 0 && proc.getState() == 2)
        proc.getResult().files[t].revealToUser();
}

bool SplitEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aif" || ext == ".aiff" || ext == ".ogg" || ext == ".m4a" || ext == ".wma")
            return true;
    }
    return false;
}

void SplitEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    if (files.size() > 0) proc.loadFile (juce::File (files[0]));
    repaint();
}
