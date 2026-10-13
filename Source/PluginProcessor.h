#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "Dsp.h"

struct SplitResult
{
    double bpm = 0.0;
    float  bpmConf = 0.f, keyConf = 0.f;
    int    keyRoot = -1, relRoot = -1;
    bool   minor = false;
    juce::File files[3];                 // 0 melody, 1 drums+perc, 2 bass
    std::vector<float> peaks[4];         // 0 mix, 1 melody, 2 drums, 3 bass
};

class SplitProcessor : public juce::AudioProcessor
{
public:
    SplitProcessor();
    ~SplitProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Disco Sauce Splitt"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    // ---- API used by the editor (message thread)
    void startSplit();
    bool loadFile (const juce::File& f);
    void halveDoubleBpm();
    juce::String getInputInfo() const;
    juce::String getStatus() const;
    SplitResult getResult() const;
    int   getState() const    { return state.load(); }          // 0 idle, 1 working, 2 done, 3 error
    float getProgress() const { return progress.load(); }
    float getStemLevel() const { return stemLevel.load(); }
    void  setStemLevel (float v) { stemLevel.store (juce::jlimit (0.f, 1.f, v)); }

private:
    class Worker : public juce::Thread
    {
    public:
        explicit Worker (SplitProcessor& o) : juce::Thread ("DiscoSauceSplitWorker"), owner (o) {}
        void run() override { owner.doSplit(); }
        SplitProcessor& owner;
    };

    void doSplit();
    void setStatus (const juce::String& s);
    static juce::String noteName (int pc);

    // live capture
    juce::AudioBuffer<float> capBuf;
    std::atomic<int> capLen { 0 };
    double capRate = 44100.0;
    bool wasPlaying = false;

    // loaded file
    juce::AudioBuffer<float> fileBuf;
    double fileRate = 44100.0;
    juce::String loadedName;
    std::atomic<bool> usingLoaded { false };

    // work copy
    std::vector<float> wIn[2];
    double workRate = 44100.0;
    juce::String workName;

    std::atomic<int> state { 0 };
    std::atomic<float> progress { 0.f };
    std::atomic<float> stemLevel { 0.8f };   // 0.8 == 0 dB, range -24 dB .. +6 dB

    mutable juce::CriticalSection lock;
    juce::String status;
    SplitResult result;
    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplitProcessor)
};
