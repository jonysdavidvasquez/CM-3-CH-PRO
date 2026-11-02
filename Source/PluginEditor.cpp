#include "PluginEditor.h"
#include <BinaryData.h>

static std::vector<std::byte> toVec (const char* d, int n)
{
    auto p = reinterpret_cast<const std::byte*> (d);
    return std::vector<std::byte> (p, p + n);
}

CM3CHEditor::CM3CHEditor (CM3CHAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    using Opt = juce::WebBrowserComponent::Options;

    web = std::make_unique<juce::WebBrowserComponent> (
        Opt{}.withBackend (Opt::Backend::webview2)
             .withWinWebView2Options (Opt::WinWebView2{}.withUserDataFolder (
                 juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CM3CHPro_WebView2")))
             .withNativeIntegrationEnabled()
             .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
             .withEventListener ("ready", [this] (const juce::var&) { ready = true; })
             .withEventListener ("setParam", [this] (const juce::var& v)
             {
                 if (auto* prm = proc.apvts.getParameter (v["id"].toString()))
                 {
                     if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
                     {
                         const auto range = ranged->getNormalisableRange();
                         const float raw = (float) (double) v["v"];
                         const float clamped = juce::jlimit (range.start, range.end, raw);
                         prm->setValueNotifyingHost (clamped);
                     }
                     else
                     {
                         prm->setValueNotifyingHost ((float) (double) v["v"]);
                     }
                 }
             })
             .withEventListener ("gesture", [this] (const juce::var& v)
             {
                 if (auto* prm = proc.apvts.getParameter (v["id"].toString()))
                 {
                     if ((bool) v["on"])
                         prm->beginChangeGesture();
                     else
                         prm->endChangeGesture();
                 }
             })
             .withEventListener ("size", [this] (const juce::var& v)
             {
                 const int h = (int) v["h"];
                 juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<CM3CHEditor> (this), h]
                 {
                     if (sp != nullptr)
                         sp->setSize (680, juce::jlimit (400, 900, h));
                 });
             }));

    addAndMakeVisible (*web);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setSize (680, 638);
    startTimerHz (30);
}

CM3CHEditor::~CM3CHEditor() { stopTimer(); }

void CM3CHEditor::resized() { web->setBounds (getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> CM3CHEditor::getResource (const juce::String& url)
{
    const auto path = url == "/" ? juce::String ("index.html") : url.fromFirstOccurrenceOf ("/", false, false);
    if (path == "index.html")
        return juce::WebBrowserComponent::Resource { toVec (BinaryData::index_html, BinaryData::index_htmlSize), "text/html" };
    return std::nullopt;
}

void CM3CHEditor::timerCallback()
{
    if (! ready)
        return;

    proc.computeSpectrum (spec);

    juce::Array<juce::var> sp, gr;
    for (float x : spec) sp.add (x);
    for (auto& g : proc.grDb) gr.add (g.load());

    juce::DynamicObject::Ptr params = new juce::DynamicObject();
    for (auto* prm : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            params->setProperty (r->paramID, r->getValue());

    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("spec", sp);
    o->setProperty ("gr", gr);
    o->setProperty ("lvc", (float) proc.lvcReductionDb.load());
    o->setProperty ("p", juce::var (params.get()));

    web->emitEventIfBrowserIsVisible ("state", juce::var (o.get()));
}