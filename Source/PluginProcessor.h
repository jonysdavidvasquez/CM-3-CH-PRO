#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

class CM3CHAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kBands = 3, kBins = 31, kFFTOrder = 13, kFFTSize = 1 << kFFTOrder;
    enum { TH, RA, GA, AT, RE, KN, SAM, SAT, LIM, MU, X10, kBandParams };

    CM3CHAudioProcessor();
    ~CM3CHAudioProcessor() override { stopTimer(); }
    void prepareToPlay (double sr, int block) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CM 3 CH PRO"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void computeSpectrum (std::array<float, kBins>& outDb);   // llamar desde el hilo de UI

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>, kBands> grDb;               // reducción de ganancia por banda (dB)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- modo FIR (fase lineal) ----
    void timerCallback() override;                 // hilo de mensajes: recarga IRs y reporta latencia
    void updateIRs (bool force);
    juce::AudioBuffer<float> makeLowpassIR (double fc) const;

    struct Delay
    {
        std::vector<float> b; int mask = 0, pos = 0;
        void prepare (int maxD) { int sz = 1; while (sz < maxD + 2) sz <<= 1; b.assign ((size_t) sz, 0.f); mask = sz - 1; pos = 0; }
        float process (float x, int d)
        {
            b[(size_t) (pos & mask)] = x;
            const float y = b[(size_t) ((pos - d) & mask)];
            ++pos; return y;
        }
    };
    juce::dsp::Convolution convLo, convHi;
    juce::AudioBuffer<float> bufLo, bufHi;
    Delay xDelay[2], dryDelay[2];
    int firM = 1024, firLat = 1024, maxBlock = 512;
    bool useFir = false;
    float modeFade = 1.f;
    double lastLo = -1.0, lastHi = -1.0;
    std::atomic<bool> prepared { false };

    std::atomic<float>* bp[kBands][kBandParams] {};
    std::atomic<float> *pLo {}, *pHi {}, *pIn {}, *pOut {}, *pByp {}, *pLvc {}, *pFir {};

    juce::dsp::LinkwitzRileyFilter<float> xLo, xHi, apLow;
    juce::AudioBuffer<float> dry;
    double fs = 44100.0;
    float env[kBands] {}, muteG[kBands] { 1, 1, 1 }, bypMix = 0.f;

    std::vector<float> fifo, fftBuf;
    std::atomic<int> fifoPos { 0 };
    juce::dsp::FFT fft { kFFTOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFFTSize, juce::dsp::WindowingFunction<float>::hann };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CM3CHAudioProcessor)
};
