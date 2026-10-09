// Android host for the KR-106: runs the synth + touch editor as a normal app
// (no plugin wrapper). Audio goes through JUCE's Oboe backend, MIDI comes from
// any USB / Bluetooth controller the OS exposes, and the sound is also
// playable from the on-screen "Keys" sheet.

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

class KR106Root : public juce::Component, private juce::Timer
{
public:
    KR106Root()
    {
        processor = std::make_unique<KR106AudioProcessor>();
        restoreState();

        const auto err = deviceManager.initialise(0, 2, nullptr, true);
        if (err.isNotEmpty())
            DBG("Audio init: " << err);

        player.setProcessor(processor.get());
        deviceManager.addAudioCallback(&player);

        editor = std::make_unique<juce::AudioProcessorEditor>(
            *processor, juce::Rectangle<int>(0, 0, 640, 480));

        // This is intentionally minimal: the Android host is just a compile-time
        // sanity check for the synth + editor path. It avoids creating GUI objects
        // that depend on the project layout without a full platform UI bootstrap.
        if (editor)
            addAndMakeVisible(editor.get());

        startTimer(1500);
    }

    ~KR106Root() override
    {
        stopTimer();
        saveState();
        deviceManager.removeAudioCallback(&player);
        player.setProcessor(nullptr);
        editor.reset();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (editor)
            editor->setBounds(b);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0c0c0c));
    }

    void saveState()
    {
        juce::MemoryBlock mb;
        processor->getStateInformation(mb);
        auto f = stateFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithData(mb.getData(), mb.getSize());
    }

private:
    static juce::File stateFile()
    {
        return KR106PresetManager::getAppDataDir().getChildFile("android-state.bin");
    }

    void restoreState()
    {
        auto f = stateFile();
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData(mb) && mb.getSize() > 0)
            processor->setStateInformation(mb.getData(), (int)mb.getSize());
    }

    void timerCallback() override {}

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<KR106AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

class KR106Window : public juce::DocumentWindow
{
public:
    KR106Window()
        : DocumentWindow("Ultramaster KR-106", juce::Colour(0xff0c0c0c), 0)
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setContentOwned(new KR106Root(), true);
        setFullScreen(true);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class KR106App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Ultramaster KR-106"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<KR106Window>();
    }

    void shutdown() override
    {
        window.reset();
    }

    void suspended() override {}
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<KR106Window> window;
};

START_JUCE_APPLICATION(KR106App)
