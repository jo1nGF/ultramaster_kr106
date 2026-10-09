// Android host for the KR-106: runs the synth + touch editor as a normal app
// (no plugin wrapper). Audio goes through JUCE's Oboe backend, MIDI comes from
// any USB / Bluetooth controller the OS exposes, and the sound is also
// playable from the on-screen "Keys" sheet.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class KR106Root : public juce::Component, private juce::Timer
{
public:
  KR106Root()
  {
    processor = std::make_unique<KR106AudioProcessor>();
    restoreState();

    auto err = deviceManager.initialise(0, 2, nullptr, true);   // stereo out, no input
    if (err.isNotEmpty()) DBG("Audio init: " << err);

    player.setProcessor(processor.get());
    deviceManager.addAudioCallback(&player);
    deviceManager.addMidiInputDeviceCallback({}, &player);       // all enabled MIDI inputs

    editor.reset(processor->createEditor());
    addAndMakeVisible(*editor);

    scanMidi();
    startTimer(1500);   // pick up controllers plugged in later
  }

  ~KR106Root() override
  {
    stopTimer();
    saveState();
    deviceManager.removeMidiInputDeviceCallback({}, &player);
    deviceManager.removeAudioCallback(&player);
    player.setProcessor(nullptr);
    editor.reset();
  }

  void resized() override
  {
    auto b = getLocalBounds();
    // Keep clear of display cut-outs (S10 punch-hole camera, gesture areas)
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
      b = d->safeAreaInsets.subtractedFrom(b);
    if (editor) editor->setBounds(b);
  }

  void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0c0c0c)); }

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
      processor->setStateInformation(mb.getData(), (int) mb.getSize());
  }

  void scanMidi()
  {
    for (auto& d : juce::MidiInput::getAvailableDevices())
      if (!deviceManager.isMidiInputDeviceEnabled(d.identifier))
        deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
  }
  void timerCallback() override { scanMidi(); }

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
    root = new KR106Root();
    setContentOwned(root, false);
    setFullScreen(true);
    setVisible(true);
    juce::Desktop::getInstance().setKioskModeComponent(this, false);   // immersive: hide system bars
  }
  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
  void save() { if (root) root->saveState(); }

private:
  KR106Root* root = nullptr;
};

class KR106App : public juce::JUCEApplication
{
public:
  const juce::String getApplicationName() override    { return "Ultramaster KR-106"; }
  const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
  bool moreThanOneInstanceAllowed() override          { return false; }

  void initialise(const juce::String&) override
  {
    auto& desk = juce::Desktop::getInstance();
    desk.setOrientationsEnabled(juce::Desktop::rotatedClockwise | juce::Desktop::rotatedAntiClockwise);
    desk.setScreenSaverEnabled(false);          // keep the screen on while playing
    window = std::make_unique<KR106Window>();
  }
  void shutdown() override { if (window) window->save(); window.reset(); }
  void suspended() override { if (window) window->save(); }
  void systemRequestedQuit() override { quit(); }

private:
  std::unique_ptr<KR106Window> window;
};

START_JUCE_APPLICATION(KR106App)
