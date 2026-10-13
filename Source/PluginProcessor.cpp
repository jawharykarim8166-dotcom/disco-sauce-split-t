#include "PluginProcessor.h"
#include "PluginEditor.h"

SplitProcessor::SplitProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    status = "PRESS SPLIT TO ANALYZE AND SPLIT YOUR TRACK";
}

SplitProcessor::~SplitProcessor()
{
    if (worker != nullptr)
    {
        worker->signalThreadShouldExit();
        worker->stopThread (20000);
    }
}

juce::AudioProcessorEditor* SplitProcessor::createEditor() { return new SplitEditor (*this); }

bool SplitProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void SplitProcessor::prepareToPlay (double sampleRate, int)
{
    capRate = sampleRate;
    const int want = juce::jmin ((int) (sampleRate * 300.0), 16000000);   // up to 5 minutes of live capture
    if (capBuf.getNumSamples() != want)
    {
        capBuf.setSize (2, want, false, true, false);
        capLen.store (0);
    }
}

void SplitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (nch < 1 || capBuf.getNumSamples() == 0) return;

    bool playing = true;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();

    if (playing && ! wasPlaying)
    {
        capLen.store (0);              // new playback pass -> fresh capture
        usingLoaded.store (false);
    }
    wasPlaying = playing;

    if (playing && state.load() != 1)
    {
        const int len = capLen.load();
        const int m = juce::jmin (n, capBuf.getNumSamples() - len);
        if (m > 0)
        {
            for (int c = 0; c < 2; ++c)
                capBuf.copyFrom (c, len, buffer, juce::jmin (c, nch - 1), 0, m);
            capLen.store (len + m);
        }
    }
    // audio passes through untouched
}

//==============================================================================
void SplitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream s (dest, true);
    s.writeFloat (stemLevel.load());
}

void SplitProcessor::setStateInformation (const void* data, int size)
{
    if (size >= (int) sizeof (float))
    {
        juce::MemoryInputStream s (data, (size_t) size, false);
        setStemLevel (s.readFloat());
    }
}

//==============================================================================
void SplitProcessor::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (lock);
    status = s;
}

juce::String SplitProcessor::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

SplitResult SplitProcessor::getResult() const
{
    const juce::ScopedLock sl (lock);
    return result;
}

void SplitProcessor::halveDoubleBpm()
{
    const juce::ScopedLock sl (lock);
    if (result.bpm > 0.0)
        result.bpm = (result.bpm >= 100.0) ? result.bpm * 0.5 : result.bpm * 2.0;
}

juce::String SplitProcessor::noteName (int pc)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return (pc >= 0 && pc < 12) ? juce::String (names[pc]) : juce::String ("--");
}

static juce::String mmss (double seconds)
{
    const int s = (int) seconds;
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

juce::String SplitProcessor::getInputInfo() const
{
    if (usingLoaded.load() && fileBuf.getNumSamples() > 0)
        return "LOADED: " + loadedName.toUpperCase() + "  " + mmss (fileBuf.getNumSamples() / fileRate);
    const int len = capLen.load();
    if (len > 0)
        return "CAPTURED " + mmss (len / capRate) + " OF YOUR TRACK - READY TO SPLIT";
    return "PLAY YOUR TRACK THROUGH ME OR DROP AN AUDIO FILE HERE";
}

//==============================================================================
bool SplitProcessor::loadFile (const juce::File& f)
{
    if (state.load() == 1) return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples < 1000)
    {
        setStatus ("COULD NOT READ THAT FILE");
        return false;
    }
    const int len = (int) juce::jmin ((juce::int64) (reader->sampleRate * 600.0), reader->lengthInSamples);   // max 10 min
    fileBuf.setSize (2, len, false, true, false);
    reader->read (&fileBuf, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        fileBuf.copyFrom (1, 0, fileBuf, 0, 0, len);
    fileRate = reader->sampleRate;
    loadedName = f.getFileNameWithoutExtension();
    usingLoaded.store (true);
    state.store (0);
    progress.store (0.f);
    setStatus ("FILE LOADED - PRESS SPLIT");
    return true;
}

void SplitProcessor::startSplit()
{
    if (state.load() == 1) return;

    if (usingLoaded.load() && fileBuf.getNumSamples() > 0)
    {
        const int len = fileBuf.getNumSamples();
        for (int c = 0; c < 2; ++c)
            wIn[c].assign (fileBuf.getReadPointer (c), fileBuf.getReadPointer (c) + len);
        workRate = fileRate;
        workName = loadedName;
    }
    else
    {
        const int len = capLen.load();
        if (len <= 0)
        {
            state.store (3);
            setStatus ("NO AUDIO YET - PLAY YOUR TRACK THROUGH ME OR DROP A FILE");
            return;
        }
        for (int c = 0; c < 2; ++c)
            wIn[c].assign (capBuf.getReadPointer (c), capBuf.getReadPointer (c) + len);
        workRate = capRate;
        workName = "Capture";
    }

    if ((double) wIn[0].size() < workRate * 4.0)
    {
        state.store (3);
        setStatus ("TOO SHORT - NEED AT LEAST 4 SECONDS OF AUDIO");
        return;
    }

    state.store (1);
    progress.store (0.f);
    setStatus ("SEPARATING STEMS...");
    worker.reset (new Worker (*this));
    worker->startThread();
}

//==============================================================================
void SplitProcessor::doSplit()
{
    auto stop = [] { return juce::Thread::currentThreadShouldExit(); };
    const double sr = workRate;
    const int total = (int) wIn[0].size();

    ds::Stems st;
    auto prog = [this] (float p) { progress.store (p * 0.7f); };
    if (! ds::separate (wIn, sr, st, prog, stop))
    {
        state.store (0);
        setStatus ("CANCELLED");
        return;
    }

    SplitResult res;
    res.peaks[0] = ds::peaksOf (wIn, 142);
    res.peaks[1] = ds::peaksOf (st.mel, 142);
    res.peaks[2] = ds::peaksOf (st.drm, 142);
    res.peaks[3] = ds::peaksOf (st.bas, 142);

    // ---------------- BPM (mix + drums stem together)
    progress.store (0.72f);
    setStatus ("DETECTING BPM...");
    {
        const int hop = 512;
        auto e1 = ds::onsetEnvelope (ds::monoOf (wIn), sr, hop, stop);
        auto e2 = ds::onsetEnvelope (ds::monoOf (st.drm), sr, hop, stop);
        std::vector<float> env = e1;
        if (! env.empty() && e2.size() == env.size())
            for (size_t i = 0; i < env.size(); ++i) env[i] = 0.5f * (e1[i] + e2[i]);
        const auto t = ds::estimateTempo (env, sr / (double) hop);
        res.bpm = t.bpm;
        res.bpmConf = t.conf;
    }

    // ---------------- KEY + SCALE (melody + bass stems)
    progress.store (0.82f);
    setStatus ("DETECTING KEY AND SCALE...");
    {
        std::vector<float> hm ((size_t) total), bm ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            hm[(size_t) i] = 0.25f * (st.mel[0][(size_t) i] + st.mel[1][(size_t) i] + st.bas[0][(size_t) i] + st.bas[1][(size_t) i]);
            bm[(size_t) i] = 0.5f * (st.bas[0][(size_t) i] + st.bas[1][(size_t) i]);
        }
        const double tuning = ds::estimateTuning (hm, sr, stop);
        double cH[12], cB[12], c[12];
        ds::chromaOf (hm, sr, 60.0, 2500.0, tuning, cH, stop);
        ds::chromaOf (bm, sr, 35.0, 260.0, tuning, cB, stop);
        double sH = 0, sB = 0;
        for (int i = 0; i < 12; ++i) { sH += cH[i]; sB += cB[i]; }
        for (int i = 0; i < 12; ++i)
            c[i] = (sH > 1e-9 ? cH[i] / sH : 0.0) + 0.7 * (sB > 1e-9 ? cB[i] / sB : 0.0);
        const auto k = ds::keyFromChroma (c);
        res.keyRoot = k.root; res.minor = k.minor; res.keyConf = k.conf; res.relRoot = k.relRoot;
    }
    if (stop()) { state.store (0); setStatus ("CANCELLED"); return; }

    // ---------------- write the three stems
    progress.store (0.9f);
    setStatus ("WRITING STEM FILES...");
    for (auto& v : wIn) std::vector<float>().swap (v);

    juce::String tag;
    if (res.bpm > 0.0) tag << " " << juce::String (juce::roundToInt (res.bpm)) << "BPM";
    if (res.keyRoot >= 0) tag << " " << noteName (res.keyRoot) << (res.minor ? "m" : "");

    const juce::File dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("Disco Sauce Splitt Stems")
                               .getChildFile (juce::File::createLegalFileName (workName + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S")));
    dir.createDirectory();

    const float level = stemLevel.load();
    const float gain = juce::Decibels::decibelsToGain ((level - 0.8f) * 30.0f);   // -24 dB .. +6 dB

    auto writeStem = [&] (std::vector<float>* ch, const juce::String& label) -> juce::File
    {
        const juce::File f = dir.getChildFile (juce::File::createLegalFileName (workName + tag + " - " + label + ".wav"));
        float peak = 0.f;
        for (int c = 0; c < 2; ++c)
            for (auto& v : ch[c]) { v *= gain; peak = juce::jmax (peak, std::abs (v)); }
        if (peak > 0.999f)
            for (int c = 0; c < 2; ++c)
                for (auto& v : ch[c]) v *= 0.999f / peak;

        std::unique_ptr<juce::FileOutputStream> fos (f.createOutputStream());
        if (fos == nullptr) return {};
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fos.get(), sr, 2, 24, juce::StringPairArray(), 0));
        if (w == nullptr) return {};
        fos.release();                                   // writer owns the stream now
        const float* data[2] = { ch[0].data(), ch[1].data() };
        w->writeFromFloatArrays (data, 2, (int) ch[0].size());
        w.reset();
        return f;
    };

    res.files[0] = writeStem (st.mel, "Melody");
    progress.store (0.94f);
    res.files[1] = writeStem (st.drm, "Drums and Perc");
    progress.store (0.97f);
    res.files[2] = writeStem (st.bas, "Bass");

    const bool ok = res.files[0].existsAsFile() && res.files[1].existsAsFile() && res.files[2].existsAsFile();
    {
        const juce::ScopedLock sl (lock);
        result = res;
    }
    progress.store (1.f);
    if (ok)
    {
        setStatus ("DONE - DRAG THE 3 STEMS INTO THE FL STUDIO PLAYLIST");
        state.store (2);
    }
    else
    {
        setStatus ("COULD NOT WRITE FILES - CHECK YOUR DOCUMENTS FOLDER");
        state.store (3);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SplitProcessor(); }
