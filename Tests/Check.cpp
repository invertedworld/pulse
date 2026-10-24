// Loads a built plugin the way a host does, for the shared Windows build
// (aax-tools/windows/build-plugin.ps1) and anyone checking a build by hand:
//   PulseCheck --host <Pulse.vst3>

#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

namespace
{

constexpr double kSR    = 48000.0;
constexpr int    kBlock = 512;

// Two instances, audio through both, and the editor opened and closed.
int hostPlugin (const juce::String& path)
{
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (found, path);

    if (found.isEmpty())
    {
        std::printf ("FAIL: no plugin found in %s\n", path.toRawUTF8());
        return 1;
    }

    const auto& description = *found.getFirst();
    std::printf ("  %s %s, %s\n", description.pluginFormatName.toRawUTF8(), description.name.toRawUTF8(),
                 description.version.toRawUTF8());

    std::vector<std::unique_ptr<juce::AudioPluginInstance>> instances;
    for (int i = 0; i < 2; ++i)
    {
        juce::String error;
        auto instance = formats.createPluginInstance (description, kSR, kBlock, error);
        if (instance == nullptr)
        {
            std::printf ("FAIL: couldn't create an instance: %s\n", error.toRawUTF8());
            return 1;
        }
        instance->setPlayConfigDetails (2, 2, kSR, kBlock);
        instance->prepareToPlay (kSR, kBlock);
        instances.push_back (std::move (instance));
    }

    std::mt19937 rng (11);
    std::uniform_real_distribution<float> noise (-0.1f, 0.1f);
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int block = 0; block < 400; ++block)
        for (auto& instance : instances)
        {
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < kBlock; ++k)
                    buffer.setSample (c, k, noise (rng));
            instance->processBlock (buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < kBlock; ++k)
                    finite = finite && std::isfinite (buffer.getSample (c, k));
        }
    std::printf ("  400 blocks through two instances: %s\n", finite ? "ok" : "FAIL, output not finite");

    bool editorOk = true;
    for (auto& instance : instances)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor { instance->createEditorIfNeeded() };
        editorOk = editorOk && editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }
    std::printf ("  editors open and close: %s\n", editorOk ? "ok" : "FAIL");

    for (auto& instance : instances)
        instance->releaseResources();
    instances.clear();
    return finite && editorOk ? 0 : 1;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc >= 3 && juce::String (argv[1]) == "--host")
        return hostPlugin (juce::String (argv[2]));

    std::printf ("usage: PulseCheck --host <Pulse.vst3>\n");
    return 2;
}
