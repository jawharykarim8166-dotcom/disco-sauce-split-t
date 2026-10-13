#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SplitEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    public juce::DragAndDropContainer,
                    private juce::Timer
{
public:
    explicit SplitEditor (SplitProcessor&);
    ~SplitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    float scale() const { return (float) getWidth() / 1000.0f; }
    juce::Point<float> toDesign (const juce::MouseEvent& e) const { return e.position / scale(); }
    int tileAt (juce::Point<float> p) const;
    static juce::Rectangle<float> tileRect (int i) { return { 56.f, 716.f + 75.f * (float) i, 480.f, 66.f }; }
    float thumbY() const { return 440.f + (1.f - proc.getStemLevel()) * 380.f; }

    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Colour,
               juce::Justification, float kern = 0.f);
    void drawLcd (juce::Graphics&);
    void drawTiles (juce::Graphics&);
    void drawPress (juce::Graphics&);

    SplitProcessor& proc;
    juce::Image bg, scaledBg;
    float phase = 0.f;
    bool pressed = false, dropHover = false, draggingThumb = false, dragStarted = false;
    int dragTile = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplitEditor)
};
