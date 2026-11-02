#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    const char* kKeys[] = { "th","ra","ga","at","re","kn","sam","sat","lim","mu","x10" };
    const char* kNames[] = { "Threshold","Ratio","Gain","Attack","Release","Knee","Sat Amount","Sat","Limit","Mute","x10" };
    const char* kBandName[] = { "Low","Mid","High" };

    // valores por defecto = los del HTML
    struct BD { float th, ra, ga, at, re, kn, sam; bool sat, lim; };
    const BD kDef[3] = { {.825f,.339f,.5f,.388f,.259f,.219f,0.f,false,false},
                         {.835f,.333f,.5f,.393f,.265f,.213f,0.f,false,false},
                         {.839f,.333f,.5f,.396f,.265f,.200f,0.f,false,false} };

    inline float staticGR (float L, float T, float R, float W)
    {
        const float over = L - T; float out;
        if (W < 0.01f)               out = over > 0.f ? T + over / R : L;
        else if (2.f * over < -W)    out = L;
        else if (2.f * over <= W)  { const float q = over + W * 0.5f; out = L + (1.f / R - 1.f) * q * q / (2.f * W); }
        else                         out = T + over / R;
        return L - out;
    }

    inline float softClip (float x, float amount)
    {
        const float a = std::abs (x);
        const float threshold = 0.35f + (1.f - amount) * 0.65f;
        if (a <= threshold)
            return x;

        const float s = (a - threshold) / (1.f - threshold);
        return std::copysign (threshold + (1.f - threshold) * std::tanh (s), x);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout CM3CHAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto F = [&] (String id, String n, float d) { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, n, NormalisableRange<float> (0.f, 1.f), d)); };
    auto B = [&] (String id, String n, bool d)  { l.add (std::make_unique<AudioParameterBool>  (ParameterID { id, 1 }, n, d)); };

    for (int b = 0; b < kBands; ++b)
    {
        const auto& d = kDef[b];
        const float f[] = { d.th, d.ra, d.ga, d.at, d.re, d.kn, d.sam };
        for (int k = 0; k < kBandParams; ++k)
        {
            String id = "b" + String (b) + "_" + kKeys[k], nm = String (kBandName[b]) + " " + kNames[k];
            if (k <= SAM) F (id, nm, f[k]);
            else B (id, nm, k == SAT ? d.sat : k == LIM ? d.lim : false);
        }
    }

    F ("lo", "Crossover Low", .564f);
    F ("hi", "Crossover High", .352f);
    F ("inn", "Input", .5f);
    F ("out", "Output", .5f);
    B ("byp", "Bypass", false);

    // LVC sigue siendo un botón on/off
    F ("lvc", "LVC", 1.f);
    // y la reducción se controla con un slider
    F ("lvcRed", "LVC Reduction", .35f);

    B ("fir", "FIR", false);

    return l;
}

CM3CHAudioProcessor::CM3CHAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      fifo (kFFTSize * 2, 0.f), fftBuf (kFFTSize * 2, 0.f)
{
    for (auto& g : grDb) g = 0.f;
    for (int b = 0; b < kBands; ++b)
        for (int k = 0; k < kBandParams; ++k)
            bp[b][k] = apvts.getRawParameterValue ("b" + juce::String (b) + "_" + kKeys[k]);
    pLo = apvts.getRawParameterValue ("lo");   pHi  = apvts.getRawParameterValue ("hi");
    pIn = apvts.getRawParameterValue ("inn");  pOut = apvts.getRawParameterValue ("out");
    pByp = apvts.getRawParameterValue ("byp");
    pLvc = apvts.getRawParameterValue ("lvc");
    pLvcRed = apvts.getRawParameterValue ("lvcRed");
    pFir = apvts.getRawParameterValue ("fir");
}

bool CM3CHAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CM3CHAudioProcessor::prepareToPlay (double sr, int block)
{
    fs = sr;
    juce::dsp::ProcessSpec s { sr, (juce::uint32) block, 2 };
    xLo.prepare (s); xHi.prepare (s); apLow.prepare (s);
    xLo.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    xHi.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    apLow.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    dry.setSize (2, block);
    for (int b = 0; b < kBands; ++b) { env[b] = 0.f; muteG[b] = 1.f; }
    bypMix = pByp->load() > 0.5f ? 1.f : 0.f;
}

void CM3CHAudioProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    if (buf.getNumChannels() < 2) return;
    if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);
    for (int c = 0; c < 2; ++c) dry.copyFrom (c, 0, buf, c, 0, n);

    const float loHz = 20.f * std::pow (25.f, pLo->load());
    const float hiHz = 2000.f * std::pow (10.f, pHi->load());
    xLo.setCutoffFrequency (loHz); xHi.setCutoffFrequency (hiHz); apLow.setCutoffFrequency (hiHz);

    struct BC { float T, R, W, mk, aC, rC, drive, satN; bool sat, mu; } bc[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        auto P = [&] (int k) { return bp[b][k]->load(); };
        auto& c = bc[b];
        c.T  = -60.f + 60.f * P (TH);
        c.R  = P (LIM) > 0.5f ? 100.f : 1.f + 19.f * P (RA) * P (RA);
        c.W  = 24.f * P (KN);
        c.mk = std::pow (10.f, (-12.f + 24.f * P (GA)) / 20.f);
        const float attMs = 0.1f * std::pow (1000.f, P (AT)), relMs = 5.f * std::pow (200.f, P (RE));
        c.aC = 1.f - std::exp (-1.f / ((float) fs * attMs * 0.001f));
        c.rC = 1.f - std::exp (-1.f / ((float) fs * relMs * 0.001f));
        c.sat = P (SAT) > 0.5f;  c.mu = P (MU) > 0.5f;
        c.drive = 1.f + P (SAM) * (P (X10) > 0.5f ? 30.f : 3.f);
        c.satN = 1.f / std::sqrt (c.drive);
    }
    const float inG  = std::pow (10.f, (-24.f + 48.f * pIn->load())  / 20.f);
    const float outG = std::pow (10.f, (-24.f + 48.f * pOut->load()) / 20.f);

    const float bypT = pByp->load() > 0.5f ? 1.f : 0.f;
    const bool lvcOn = pLvc->load() > 0.5f;
    const float lvcAmount = juce::jlimit (0.f, 1.f, pLvcRed->load());

    float* ch[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
    const float* dr[2] = { dry.getReadPointer (0), dry.getReadPointer (1) };
    float grMax[kBands] = { 0, 0, 0 };
    int wp = fifoPos.load();

    for (int s = 0; s < n; ++s)
    {
        float low[2], rest[2], mid[2], high[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = ch[c][s] * inG;
            xLo.processSample (c, x, low[c], rest[c]);
            xHi.processSample (c, rest[c], mid[c], high[c]);
            low[c] = apLow.processSample (c, low[c]);
        }
        float* bands[kBands] = { low, mid, high };
        float sum[2] = { 0.f, 0.f };
        for (int b = 0; b < kBands; ++b)
        {
            auto& k = bc[b]; float* x = bands[b];
            const float lvl = juce::jmax (std::abs (x[0]), std::abs (x[1]));
            const float ldb = 20.f * std::log10 (juce::jmax (lvl, 1.0e-6f));
            const float gr  = staticGR (ldb, k.T, k.R, k.W);
            env[b] += (gr - env[b]) * (gr > env[b] ? k.aC : k.rC);
            grMax[b] = juce::jmax (grMax[b], env[b]);
            const float g = std::exp (-env[b] * 0.11512925f) * k.mk;
            muteG[b] += ((k.mu ? 0.f : 1.f) - muteG[b]) * 0.002f;
            for (int c = 0; c < 2; ++c)
            {
                float y = x[c] * g;
                if (k.sat) y = std::tanh (k.drive * y) * k.satN;
                sum[c] += y * muteG[b];
            }
        }
        bypMix += (bypT - bypMix) * 0.002f;
        float mono = 0.f;
        for (int c = 0; c < 2; ++c)
        {
            float y = sum[c];
            if (lvcOn) y = softClip (y, lvcAmount);
            y *= outG;
            y = y * (1.f - bypMix) + dr[c][s] * bypMix;
            ch[c][s] = y; mono += 0.5f * y;
        }
        fifo[(size_t) (wp++ & (kFFTSize * 2 - 1))] = mono;
    }
    fifoPos.store (wp);
    for (int b = 0; b < kBands; ++b) grDb[(size_t) b].store (bypT > 0.5f ? 0.f : grMax[b]);
}

void CM3CHAudioProcessor::computeSpectrum (std::array<float, kBins>& out)
{
    const int wp = fifoPos.load();
    for (int i = 0; i < kFFTSize; ++i) fftBuf[(size_t) i] = fifo[(size_t) ((wp - kFFTSize + i) & (kFFTSize * 2 - 1))];
    std::fill (fftBuf.begin() + kFFTSize, fftBuf.end(), 0.f);
    window.multiplyWithWindowingTable (fftBuf.data(), (size_t) kFFTSize);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());
    const float binHz = (float) fs / (float) kFFTSize, norm = 4.f / (float) kFFTSize;
    for (int i = 0; i < kBins; ++i)
    {
        const float f = 20.f * std::pow (2.f, (float) i / 3.f);
        const int kl = (int) std::ceil (f / 1.1225f / binHz), kh = (int) std::floor (f * 1.1225f / binHz);
        float e = 0.f;
        if (kh < kl) { const float m = fftBuf[(size_t) juce::roundToInt (f / binHz)] * norm; e = m * m; }
        else for (int k = kl; k <= kh && k < kFFTSize / 2; ++k) { const float m = fftBuf[(size_t) k] * norm; e += m * m; }
        out[(size_t) i] = juce::jlimit (-90.f, 0.f, 10.f * std::log10 (juce::jmax (e, 1.0e-10f)));
    }
}

void CM3CHAudioProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}
void CM3CHAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessorEditor* CM3CHAudioProcessor::createEditor() { return new CM3CHEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CM3CHAudioProcessor(); }