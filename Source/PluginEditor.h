#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

class CM3CHEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CM3CHEditor (CM3CHAudioProcessor&);
    ~CM3CHEditor() override;
    void resized() override;

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    CM3CHAudioProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::atomic<bool> ready { false };
    std::array<float, CM3CHAudioProcessor::kBins> spec {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CM3CHEditor)
};