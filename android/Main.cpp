// Android host for the KR-106: runs the synth + touch editor as a normal app
// (no plugin wrapper). Audio goes through JUCE's Oboe backend, MIDI comes from
// any USB / Bluetooth controller the OS exposes, and the sound is also
// playable from the on-screen "Keys" sheet.
//
// Settings > "Options: State, Audio, MIDI" opens a panel with:
//   * State: save / load / reset the whole plug-in state, and "Save as default
//     state" - that state is loaded every time the app starts
//   * audio driver, buffer size (latency), sample rate
//   * per-device MIDI input switches and a MIDI monitor
// Audio/MIDI choices are remembered in android-audio.xml next to the other app data.

#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
#include "PluginProcessor.h"
#include "KR106Touch.h"
#include "KR106TouchKeys.h"

// ---------------------------------------------------------------------------
// State files. A "state" is the complete plug-in state (every parameter incl.
// delay, arpeggiator extras and global settings) - more than a preset keeps.
static juce::File statesDir()
{
  return KR106PresetManager::getAppDataDir().getChildFile("States");
}
static juce::File defaultStateFile()
{
  return KR106PresetManager::getAppDataDir().getChildFile("default.kr106state");
}

// What the panel needs from the app to act on states
struct KR106StateOps
{
  std::function<juce::MemoryBlock()> capture;            // current state
  std::function<void(const juce::MemoryBlock&)> apply;   // load a state
  std::function<void()> factoryReset;                    // back to out-of-the-box
};

// ---------------------------------------------------------------------------
// Forwards MIDI to the player and remembers the last message for the monitor.
class KR106MidiForwarder : public juce::MidiInputCallback
{
public:
  explicit KR106MidiForwarder(juce::AudioProcessorPlayer& p) : player(p) {}

  void handleIncomingMidiMessage(juce::MidiInput* src, const juce::MidiMessage& m) override
  {
    player.handleIncomingMidiMessage(src, m);
    juce::String text = m.getDescription();
    if (!text.contains("Clock") && !text.contains("Active Sense"))   // ignore the chatty ones
    {
      const juce::SpinLock::ScopedLockType lock(mLock);
      mLast = (src ? src->getName() : juce::String("UI")) + ":  " + text;
      mCount++;
    }
  }
  juce::String getLast(int& count) const
  {
    const juce::SpinLock::ScopedLockType lock(mLock);
    count = mCount;
    return mLast;
  }

  juce::AudioProcessorPlayer& player;
private:
  mutable juce::SpinLock mLock;
  juce::String mLast;
  int mCount = 0;
};

// ---------------------------------------------------------------------------
class KR106OptionsPanel : public juce::Component, private juce::Timer
{
public:
  KR106OptionsPanel(juce::AudioDeviceManager& dm, KR106MidiForwarder& fwd,
                      std::set<juce::String>& disabledMidi, std::set<juce::String>& knownMidi,
                      std::function<void()> onSave, std::function<void()> onPanic,
                      std::function<void()> onClose, KR106StateOps stateOps)
    : deviceManager(dm), forwarder(fwd), disabled(disabledMidi), known(knownMidi),
      save(std::move(onSave)), panic(std::move(onPanic)), close(std::move(onClose)),
      ops(std::move(stateOps))
  {
    // The Close button lives outside the rebuilt content: it is created once and
    // keeps its position (rebuilding the panel used to drop it, leaving it invisible).
    closeBtn = std::make_unique<Btn>("Close", [this] { if (close) close(); }, juce::Colour(0xff9a0e0e));
    addAndMakeVisible(*closeBtn);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
    rebuild();
    startTimer(400);
  }

  void paint(juce::Graphics& g) override
  {
    g.fillAll(juce::Colour(0xff181818));
    g.setColour(juce::Colour(0xffc60a0a));
    g.fillRect(0, 54, getWidth(), 2);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.f));
    g.drawText(listMode ? "Saved states" : "Options", 16, 8, getWidth() - 160, 40, juce::Justification::centredLeft);
  }

  void resized() override
  {
    if (closeBtn) closeBtn->setBounds(getWidth() - 116, 8, 100, 40);
    viewport.setBounds(0, 60, getWidth(), getHeight() - 60);
    layoutContent();
  }

private:
  using Btn = KR106TouchTextButton;

  void timerCallback() override
  {
    int count = 0;
    auto last = forwarder.getLast(count);
    juce::String status = describeAudio();
    if (status != lastStatus || count != lastCount || last != lastMidi)
    {
      lastStatus = status; lastCount = count; lastMidi = last;
      if (statusLabel) statusLabel->setText(status, juce::dontSendNotification);
      if (midiLabel)
        midiLabel->setText(count == 0 ? "MIDI monitor: nothing received yet - press a key on your controller"
                                      : "MIDI monitor: " + last, juce::dontSendNotification);
    }
  }

  juce::String describeAudio() const
  {
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return "No audio device is open.";
    const double sr = dev->getCurrentSampleRate();
    const int buf = dev->getCurrentBufferSizeSamples();
    const int outLat = dev->getOutputLatencyInSamples();
    juce::String s = dev->getName() + " (" + deviceManager.getCurrentAudioDeviceType() + ")\n";
    s << "Buffer " << buf << " samples @ " << juce::String(sr, 0) << " Hz = "
      << juce::String(1000.0 * buf / sr, 1) << " ms per buffer";
    if (outLat > 0)
      s << "   |   reported output latency " << juce::String(1000.0 * outLat / sr, 1) << " ms";
    return s;
  }

  juce::Label* addLabel(const juce::String& text, float size, juce::Colour col, bool bold = false)
  {
    auto* l = new juce::Label({}, text);
    l->setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    l->setColour(juce::Label::textColourId, col);
    l->setJustificationType(juce::Justification::topLeft);
    labels.add(l);
    content.addAndMakeVisible(l);
    return l;
  }
  Btn* addButton(const juce::String& text, bool active, std::function<void()> fn)
  {
    auto* b = new Btn(text, std::move(fn));
    b->setActive(active);
    buttons.add(b);
    content.addAndMakeVisible(b);
    return b;
  }

  // Row layout: every element is (component, width); wraps to the next line.
  struct Item { juce::Component* c; int w; int h; };
  std::vector<Item> items;      // laid out in order by layoutContent()
  struct Break { int index; int gap; };

  void section(const juce::String& title)
  {
    auto* l = addLabel(title, 17.f, juce::Colour(0xffff5a4a), true);
    items.push_back({ l, -1, 30 });
  }
  void note(const juce::String& text, int h = 22)
  {
    auto* l = addLabel(text, 13.f, juce::Colour(0xffbdbdbd));
    items.push_back({ l, -1, h });
  }

  void rebuild()
  {
    repaint();                       // the title changes between Options / Saved states
    content.removeAllChildren();
    labels.clear(); buttons.clear(); items.clear();
    statusLabel = nullptr; midiLabel = nullptr;
    if (listMode) { buildStateList(); layoutContent(); return; }

    // ---- State ----
    section("State  (whole plug-in setup: patch, delay, arpeggiator, global settings)");
    items.push_back({ addButton("Save current state...", false, [this] { askStateName(); }), 230, 44 });
    items.push_back({ addButton("Load a saved state...", false, [this] { listMode = true; afterChange(); }), 230, 44 });
    items.push_back({ addButton("Reset to default state", false, [this] { resetToDefault(); }), 230, 44 });
    items.push_back({ addButton("Save as default state", false, [this] { saveAsDefault(); }), 230, 44 });
    items.push_back({ addButton("Remove default state", false, [this] { removeDefault(); }), 230, 44 });
    auto* msg = addLabel(stateMessage.isEmpty() ? defaultStateText() : stateMessage + "\n" + defaultStateText(),
                         13.f, juce::Colour(0xffbdbdbd));
    items.push_back({ msg, -1, 40 });

    auto* dev = deviceManager.getCurrentAudioDevice();

    // ---- Audio driver ----
    auto& types = deviceManager.getAvailableDeviceTypes();
    if (types.size() > 1)
    {
      section("Audio driver");
      for (auto* t : types)
      {
        auto name = t->getTypeName();
        items.push_back({ addButton(name, name == deviceManager.getCurrentAudioDeviceType(),
                                    [this, name] { deviceManager.setCurrentAudioDeviceType(name, true); afterChange(); }), 150, 44 });
      }
    }

    // ---- Buffer size ----
    section("Buffer size  (smaller = lower latency, may crackle)");
    juce::Array<int> sizes;
    int curBuf = 0; double curSr = 48000.0;
    if (dev != nullptr)
    {
      sizes = dev->getAvailableBufferSizes();
      curBuf = dev->getCurrentBufferSizeSamples();
      curSr = dev->getCurrentSampleRate();
    }
    if (sizes.isEmpty()) for (int s : { 64, 96, 128, 192, 256, 384, 512, 768, 1024 }) sizes.add(s);
    sizes.sort();
    int shown = 0;
    for (int sz : sizes)
    {
      if (sz < 32 || sz > 2048) continue;
      if (++shown > 14) break;
      juce::String label = juce::String(sz) + "  (" + juce::String(1000.0 * sz / curSr, 1) + " ms)";
      items.push_back({ addButton(label, sz == curBuf, [this, sz] { setBuffer(sz); }), 150, 44 });
    }

    // ---- Sample rate ----
    if (dev != nullptr)
    {
      auto rates = dev->getAvailableSampleRates();
      if (rates.size() > 1)
      {
        section("Sample rate");
        int n = 0;
        for (double r : rates)
          if (++n <= 8)
            items.push_back({ addButton(juce::String(r, 0) + " Hz", std::abs(r - curSr) < 1.0,
                                        [this, r] { setRate(r); }), 120, 44 });
      }
    }

    // ---- Status ----
    section("Status");
    statusLabel = addLabel(describeAudio(), 14.f, juce::Colours::white);
    items.push_back({ statusLabel, -1, 44 });
    note("Tip: on Android the lowest stable buffer is device dependent. If you hear crackles, pick a bigger buffer,");
    note("or reduce voices / VCF oversampling in the settings menu. Bluetooth headphones add their own delay.", 24);

    // ---- MIDI ----
    section("MIDI inputs");
    auto devices = juce::MidiInput::getAvailableDevices();
    if (devices.isEmpty())
      note("No MIDI device found. Connect a USB controller to the USB-C port (OTG adapter) and press Rescan.", 24);
    for (auto& d : devices)
    {
      const bool on = deviceManager.isMidiInputDeviceEnabled(d.identifier);
      auto id = d.identifier;
      items.push_back({ addButton((on ? "ON   " : "OFF  ") + d.name, on,
                                  [this, id] { toggleMidi(id); }), 420, 44 });
    }
    items.push_back({ addButton("Rescan", false, [this] { known.clear(); rescan(); }), 120, 44 });
    items.push_back({ addButton("All notes off", false, [this] { if (panic) panic(); }), 160, 44 });
    midiLabel = addLabel("MIDI monitor: nothing received yet - press a key on your controller", 14.f, juce::Colours::white);
    items.push_back({ midiLabel, -1, 30 });

    layoutContent();
  }

  void layoutContent()
  {
    const int W = viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? viewport.getScrollBarThickness() : 0);
    const int pad = 16, gap = 8;
    int x = pad, y = 8;
    bool rowOpen = false;
    int rowH = 0;
    for (auto& it : items)
    {
      if (it.w < 0)                              // full-width line (header / label)
      {
        if (rowOpen) { y += rowH + gap; x = pad; rowOpen = false; }
        y += 6;
        it.c->setBounds(pad, y, W - 2 * pad, it.h);
        y += it.h + 2;
        continue;
      }
      if (rowOpen && x + it.w > W - pad) { y += rowH + gap; x = pad; rowH = 0; }
      it.c->setBounds(x, y, it.w, it.h);
      x += it.w + gap; rowH = std::max(rowH, it.h); rowOpen = true;
    }
    if (rowOpen) y += rowH;
    content.setSize(W, y + 24);
  }

  // ----- state handling -----
  juce::String defaultStateText() const
  {
    return defaultStateFile().existsAsFile()
      ? "Default state: SET - it is loaded every time the app opens."
      : "Default state: not set - the app reopens with the last session.";
  }

  void buildStateList()
  {
    section("Tap a state to load it");
    auto files = statesDir().findChildFiles(juce::File::findFiles, false, "*.kr106state");
    files.sort();
    if (files.isEmpty())
      note("No saved states yet. Use \"Save current state...\" in the Options screen.", 24);
    for (auto& f : files)
    {
      auto label = f.getFileNameWithoutExtension() + "    " + f.getLastModificationTime().formatted("%d/%m %H:%M");
      items.push_back({ addButton(label, false, [this, f] { loadStateFile(f); }), 460, 44 });
      const bool pending = (pendingDelete == f.getFullPathName());
      items.push_back({ addButton(pending ? "Confirm delete" : "Delete", pending, [this, f] {
                          if (pendingDelete == f.getFullPathName()) { f.deleteFile(); pendingDelete.clear(); }
                          else pendingDelete = f.getFullPathName();
                          afterChange(); }), 150, 44 });
    }
    items.push_back({ addButton("Back", false, [this] { listMode = false; pendingDelete.clear(); afterChange(); }), 150, 44 });
  }

  void loadStateFile(const juce::File& f)
  {
    juce::MemoryBlock mb;
    if (f.loadFileAsData(mb) && mb.getSize() > 0 && ops.apply)
    {
      ops.apply(mb);
      stateMessage = "Loaded state: " + f.getFileNameWithoutExtension();
    }
    else stateMessage = "Could not read " + f.getFileName();
    listMode = false; pendingDelete.clear();
    afterChange();
  }

  void askStateName()
  {
    auto* w = new juce::AlertWindow("Save current state", "Name for this state:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "State " + juce::Time::getCurrentTime().formatted("%d-%m %H.%M"), "");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<KR106OptionsPanel> safe(this);
    w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int r)
    {
      auto name = w->getTextEditorContents("name").trim();
      delete w;
      if (r == 1 && name.isNotEmpty() && safe != nullptr) safe->writeState(name);
    }), false);
  }

  void writeState(const juce::String& name)
  {
    if (!ops.capture) return;
    auto dir = statesDir();
    dir.createDirectory();
    auto f = dir.getChildFile(juce::File::createLegalFileName(name) + ".kr106state");
    auto mb = ops.capture();
    stateMessage = f.replaceWithData(mb.getData(), mb.getSize()) ? "Saved state: " + name
                                                                  : "Could not save " + name;
    afterChange();
  }

  void resetToDefault()
  {
    juce::MemoryBlock mb;
    auto def = defaultStateFile();
    if (def.existsAsFile() && def.loadFileAsData(mb) && mb.getSize() > 0 && ops.apply)
    {
      ops.apply(mb);
      stateMessage = "Default state loaded.";
    }
    else
    {
      if (ops.factoryReset) ops.factoryReset();
      stateMessage = "No default state saved: factory defaults restored.";
    }
    afterChange();
  }

  void saveAsDefault()
  {
    if (!ops.capture) return;
    auto mb = ops.capture();
    auto def = defaultStateFile();
    def.getParentDirectory().createDirectory();
    stateMessage = def.replaceWithData(mb.getData(), mb.getSize())
      ? "Saved as default state. It loads every time the app opens."
      : "Could not save the default state.";
    afterChange();
  }

  void removeDefault()
  {
    defaultStateFile().deleteFile();
    stateMessage = "Default state removed. The app reopens with the last session again.";
    afterChange();
  }

  void afterChange()
  {
    if (save) save();
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<KR106OptionsPanel>(this)] {
      if (safe) safe->rebuild(); });
  }
  void setBuffer(int sz)
  {
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = sz;
    auto err = deviceManager.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty()) DBG("Buffer change: " << err);
    afterChange();
  }
  void setRate(double r)
  {
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = r;
    auto err = deviceManager.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty()) DBG("Rate change: " << err);
    afterChange();
  }
  void toggleMidi(const juce::String& id)
  {
    const bool nowOn = !deviceManager.isMidiInputDeviceEnabled(id);
    deviceManager.setMidiInputDeviceEnabled(id, nowOn);
    if (nowOn) disabled.erase(id); else disabled.insert(id);
    afterChange();
  }
  void rescan()
  {
    for (auto& d : juce::MidiInput::getAvailableDevices())
      if (known.insert(d.identifier).second && disabled.count(d.identifier) == 0)
        deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
    afterChange();
  }

  juce::AudioDeviceManager& deviceManager;
  KR106MidiForwarder& forwarder;
  std::set<juce::String>& disabled;
  std::set<juce::String>& known;
  std::function<void()> save, panic, close;
  KR106StateOps ops;
  bool listMode = false;
  juce::String stateMessage, pendingDelete;

  juce::Viewport viewport;
  juce::Component content;
  juce::OwnedArray<juce::Label> labels;
  juce::OwnedArray<Btn> buttons;
  std::unique_ptr<Btn> closeBtn;
  juce::Label* statusLabel = nullptr;
  juce::Label* midiLabel = nullptr;
  juce::String lastStatus, lastMidi;
  int lastCount = -1;
};

// ---------------------------------------------------------------------------
class KR106Root : public juce::Component, private juce::Timer
{
public:
  KR106Root()
  {
    processor = std::make_unique<KR106AudioProcessor>();
    restoreState();

    auto savedXml = loadAudioSettings();
    auto err = deviceManager.initialise(0, 2, savedXml.get(), true);   // stereo out, no input
    if (err.isNotEmpty()) DBG("Audio init: " << err);

    player.setProcessor(processor.get());
    forwarder = std::make_unique<KR106MidiForwarder>(player);
    deviceManager.addAudioCallback(&player);
    deviceManager.addMidiInputDeviceCallback({}, forwarder.get());   // all enabled MIDI inputs

    editor.reset(processor->createEditor());
    addAndMakeVisible(*editor);

    kr106touch::audioMidiHook() = [this] { showAudioMidi(); };

    scanMidi();
    startTimer(1500);   // pick up controllers plugged in later
  }

  ~KR106Root() override
  {
    kr106touch::audioMidiHook() = nullptr;
    stopTimer();
    panel.reset();
    saveState();
    saveAudioSettings();
    deviceManager.removeMidiInputDeviceCallback({}, forwarder.get());
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
    if (panel) panel->setBounds(b);
  }

  void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0c0c0c)); }

  // Android Back button (and Esc on desktop): close the Options panel first.
  bool handleBack()
  {
    if (!panel) return false;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<KR106Root>(this)] {
      if (safe) safe->panel.reset(); });
    return true;
  }
  bool keyPressed(const juce::KeyPress& k) override
  {
    if (k == juce::KeyPress::escapeKey) return handleBack();
    return false;
  }

  void saveState()
  {
    juce::MemoryBlock mb;
    processor->getStateInformation(mb);
    auto f = stateFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithData(mb.getData(), mb.getSize());
    saveAudioSettings();
  }

private:
  static juce::File stateFile()
  {
    return KR106PresetManager::getAppDataDir().getChildFile("android-state.bin");
  }
  static juce::File audioFile()
  {
    return KR106PresetManager::getAppDataDir().getChildFile("android-audio.xml");
  }

  // Start-up: a saved DEFAULT state wins; otherwise reopen the last session.
  void restoreState()
  {
    juce::MemoryBlock mb;
    auto def = defaultStateFile();
    if (def.existsAsFile() && def.loadFileAsData(mb) && mb.getSize() > 0)
    {
      processor->setStateInformation(mb.getData(), (int) mb.getSize());
      return;
    }
    mb.reset();
    auto f = stateFile();
    if (f.existsAsFile() && f.loadFileAsData(mb) && mb.getSize() > 0)
      processor->setStateInformation(mb.getData(), (int) mb.getSize());
  }

  // Out-of-the-box sound: build a fresh processor + editor, then put the global
  // settings (voices, oversampling, delay, arp extras ...) back to their defaults too,
  // because those are remembered on disk between runs.
  void factoryReset()
  {
    deviceManager.removeAudioCallback(&player);
    player.setProcessor(nullptr);
    editor.reset();
    processor = std::make_unique<KR106AudioProcessor>();
    for (int i = kSettingVoices; i < kNumParams; i++)
      if (auto* prm = processor->getParam(i)) prm->setValueNotifyingHost(prm->getDefaultValue());
    processor->saveGlobalSettings();
    player.setProcessor(processor.get());
    deviceManager.addAudioCallback(&player);
    editor.reset(processor->createEditor());
    addAndMakeVisible(*editor);
    editor->toBack();
    resized();
  }

  // android-audio.xml: <KR106AUDIO><DEVICESETUP .../><MIDIOFF id=".."/>...</KR106AUDIO>
  std::unique_ptr<juce::XmlElement> loadAudioSettings()
  {
    std::unique_ptr<juce::XmlElement> device;
    auto xml = juce::XmlDocument::parse(audioFile());
    if (xml != nullptr && xml->hasTagName("KR106AUDIO"))
    {
      for (auto* m : xml->getChildWithTagNameIterator("MIDIOFF"))
        disabledMidi.insert(m->getStringAttribute("id"));
      if (auto* d = xml->getChildByName("DEVICESETUP"))
        device = std::make_unique<juce::XmlElement>(*d);
    }
    return device;
  }
  void saveAudioSettings()
  {
    juce::XmlElement xml("KR106AUDIO");
    if (auto dev = deviceManager.createStateXml()) xml.addChildElement(dev.release());
    for (auto& id : disabledMidi) xml.createNewChildElement("MIDIOFF")->setAttribute("id", id);
    auto f = audioFile();
    f.getParentDirectory().createDirectory();
    xml.writeTo(f);
  }

  void scanMidi()
  {
    for (auto& d : juce::MidiInput::getAvailableDevices())
      if (known.insert(d.identifier).second && disabledMidi.count(d.identifier) == 0)
        deviceManager.setMidiInputDeviceEnabled(d.identifier, true);   // new device: enable it
  }
  void timerCallback() override { scanMidi(); }

  void showAudioMidi()
  {
    if (panel) return;
    panel = std::make_unique<KR106OptionsPanel>(
      deviceManager, *forwarder, disabledMidi, known,
      [this] { saveAudioSettings(); },
      [this] {
        for (int ch = 1; ch <= 16; ch++)
        {
          forwarder->player.handleIncomingMidiMessage(nullptr, juce::MidiMessage::allSoundOff(ch));
          forwarder->player.handleIncomingMidiMessage(nullptr, juce::MidiMessage::allNotesOff(ch));
        }
      },
      [this] { juce::MessageManager::callAsync([this] { panel.reset(); }); },
      KR106StateOps {
        [this] { juce::MemoryBlock mb; processor->getStateInformation(mb); return mb; },
        [this](const juce::MemoryBlock& mb) { processor->setStateInformation(mb.getData(), (int) mb.getSize()); },
        [this] { juce::MessageManager::callAsync([this] { factoryReset(); }); } });
    addAndMakeVisible(*panel);
    resized();
    panel->toFront(true);
    panel->grabKeyboardFocus();
  }

  juce::AudioDeviceManager deviceManager;
  juce::AudioProcessorPlayer player;
  std::unique_ptr<KR106MidiForwarder> forwarder;
  std::set<juce::String> disabledMidi, known;
  std::unique_ptr<KR106AudioProcessor> processor;
  std::unique_ptr<juce::AudioProcessorEditor> editor;
  std::unique_ptr<KR106OptionsPanel> panel;
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
  bool handleBack() { return root != nullptr && root->handleBack(); }

private:
  KR106Root* root = nullptr;
};

class KR106App : public juce::JUCEApplication
{
public:
  const juce::String getApplicationName() override    { return "Ultramaster KR-106"; }
  const juce::String getApplicationVersion() override { return "2.5.13-touch"; }
  bool moreThanOneInstanceAllowed() override          { return false; }
  bool backButtonPressed() override                   { return window && window->handleBack(); }

  void initialise(const juce::String&) override
  {
    auto& desk = juce::Desktop::getInstance();
    desk.setOrientationsEnabled(juce::Desktop::rotatedClockwise | juce::Desktop::rotatedAntiClockwise);
    desk.setScreenSaverEnabled(false);          // keep the screen on while playing
    // Dark look for the system-style dialogs (save preset etc.)
    laf.setColourScheme(juce::LookAndFeel_V4::getMidnightColourScheme());
    juce::LookAndFeel::setDefaultLookAndFeel(&laf);
    window = std::make_unique<KR106Window>();
  }
  void shutdown() override
  {
    if (window) window->save();
    window.reset();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
  }
  void suspended() override { if (window) window->save(); }
  void systemRequestedQuit() override { quit(); }

private:
  juce::LookAndFeel_V4 laf;
  std::unique_ptr<KR106Window> window;
};

START_JUCE_APPLICATION(KR106App)
