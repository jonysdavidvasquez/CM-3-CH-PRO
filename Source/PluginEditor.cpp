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
                         prm->setValueNotifyingHost (range.convertTo0to1 (clamped));
                     }
                     else
                     {
                         prm->setValueNotifyingHost (juce::jlimit (0.f, 1.f, (float) (double) v["v"]));
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
    if (path != "index.html")
        return std::nullopt;

    // Refresh the LVC visual when the DSP meter changes, without rerendering during a drag.
    auto html = juce::String::fromUTF8 (BinaryData::index_html, BinaryData::index_htmlSize);
    html = html.replace (
        "lvcAct=typeof m.lvc==='number'?m.lvc:0;var ch=false,dk=drag?gid(drag):null;",
        "var oldLvcAct=lvcAct;lvcAct=typeof m.lvc==='number'?m.lvc:0;var ch=false,dk=drag?gid(drag):null;");
    html = html.replace (
        "if(ch&&!drag)render()",
        "if(!drag&&(ch||Math.abs(oldLvcAct-lvcAct)>0.05))render()");

    return juce::WebBrowserComponent::Resource {
        toVec (html.toRawUTF8(), (int) html.getNumBytesAsUTF8()), "text/html"
    };
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
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            float value = ranged->getValue();
            if (ranged->paramID == "lvcRed")
                value = ranged->convertFrom0to1 (value);
            params->setProperty (ranged->paramID, value);
        }
    }

    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("spec", sp);
    o->setProperty ("gr", gr);
    o->setProperty ("lvc", proc.lvcReductionDb.load());
    o->setProperty ("p", juce::var (params.get()));
    web->emitEventIfBrowserIsVisible ("state", juce::var (o.get()));
}
