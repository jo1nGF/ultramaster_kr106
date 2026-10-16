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
#include "KR106Recorder.h"
#include "KR106StateIO.h"

// ---------------------------------------------------------------------------
// State files. A "state" is the complete plug-in state (every parameter incl.
// delay, arpeggiator extras and global settings) - more than a preset keeps.
static juce::File statesDir()
{
  return kr106state::dir(KR106PresetManager::getAppDataDir());
}
static juce::File recordingsDir()
{
  return KR106PresetManager::getAppDataDir().getChildFile("Recordings");
}
static juce::File exportDir()
{
  return KR106PresetManager::getAppDataDir().getChildFile("Export");
}

// ---- sharing / saving a file outside the app's private folder -------------------------
// Share: the Android share sheet (Drive, e-mail, messengers, ...).
static void shareFile(const juce::File& f, std::function<void(const juce::String&)> done)
{
#if JUCE_CONTENT_SHARING      // Android (enabled in KR106Touch.jucer) - not available on desktop builds
  juce::Array<juce::URL> urls;
  urls.add(juce::URL(f));
  static juce::ScopedMessageBox sharing;      // must stay alive while the share sheet is open
  sharing = juce::ContentSharer::shareFilesScoped(urls, [done](bool ok, const juce::String& err)
  {
    if (done) done(ok ? juce::String() : (err.isEmpty() ? juce::String("Sharing is not available here.") : err));
  });
#else
  if (done) done("Sharing is only available in the Android app. File: " + f.getFullPathName());
#endif
}
// Android returns content:// URIs from the system pickers. URL::createInputStream() only opens
// file:// paths (content:// goes to the HTTP stream and fails), so those go through AndroidDocument.
static bool readPickedFile(const juce::URL& url, juce::String& name, juce::MemoryBlock& mb)
{
  name = url.getFileName();
  if (url.getScheme() == "content")
  {
    auto doc = juce::AndroidDocument::fromDocument(url);
    if (! doc.hasValue()) return false;
    name = doc.getInfo().getName();
    auto in = doc.createInputStream();
    if (in == nullptr) return false;
    in->readIntoMemoryBlock(mb);
    return mb.getSize() > 0;
  }
  std::unique_ptr<juce::InputStream> in(url.createInputStream(
    juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)));
  if (in == nullptr) return false;
  in->readIntoMemoryBlock(mb);
  return mb.getSize() > 0;
}

static std::unique_ptr<juce::OutputStream> openPickedForWriting(const juce::URL& url)
{
  if (url.getScheme() == "content")
  {
    auto doc = juce::AndroidDocument::fromDocument(url);
    return doc.hasValue() ? doc.createOutputStream() : nullptr;
  }
  return url.createOutputStream();
}

// Save as: the system document picker (any folder, Drive, USB ...).
static void saveFileAs(const juce::File& src, const juce::String& suggestedName, const juce::String& wildcard,
                       std::function<void(const juce::String&)> done)
{
  auto chooser = std::make_shared<juce::FileChooser>(
    "Save as", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(suggestedName), wildcard);
  chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
    [chooser, src, done](const juce::FileChooser& fc)
    {
      auto url = fc.getURLResult();
      if (url.isEmpty()) return;                                   // cancelled
      std::unique_ptr<juce::InputStream> in(src.createInputStream());
      auto out = openPickedForWriting(url);
      const bool ok = in != nullptr && out != nullptr && out->writeFromInputStream(*in, -1) > 0;
      if (out) out->flush();
      if (done) done(ok ? "Saved: " + url.getFileName() : juce::String("Could not save the file."));
    });
}
// Import: pick one or more state files / zips and hand each one's bytes over (ok = could read it).
static void pickImport(std::function<void(const juce::String& name, const juce::MemoryBlock&, bool ok)> each,
                       std::function<void()> finished)
{
  auto chooser = std::make_shared<juce::FileChooser>("Import state or zip", juce::File(), "*");   // "*": the picker has no MIME type for .kr106state
  chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems,
    [chooser, each, finished](const juce::FileChooser& fc)
    {
      for (auto& url : fc.getURLResults())
      {
        juce::String name;
        juce::MemoryBlock mb;
        const bool ok = readPickedFile(url, name, mb);
        if (each) each(name, mb, ok);
      }
      if (finished) finished();
    });
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
    g.drawText(listMode == 1 ? "Saved states" : listMode == 2 ? "Recordings" : "Options", 16, 8, getWidth() - 160, 40, juce::Justification::centredLeft);
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
    if (listMode == 1) { buildStateList(); layoutContent(); return; }
    if (listMode == 2) { buildRecordingList(); layoutContent(); return; }

    // ---- State ----
    section("State  (whole plug-in setup: patch, delay, arpeggiator, global settings)");
    items.push_back({ addButton("Save current state...", false, [this] { askStateName(); }), 230, 44 });
    items.push_back({ addButton("Load a saved state...", false, [this] { listMode = 1; afterChange(); }), 230, 44 });
    items.push_back({ addButton("Reset to default state", false, [this] { resetToDefault(); }), 230, 44 });
    items.push_back({ addButton("Save as default state", false, [this] { saveAsDefault(); }), 230, 44 });
    items.push_back({ addButton("Remove default state", false, [this] { removeDefault(); }), 230, 44 });
    items.push_back({ addButton("Export all states (.zip)", false, [this] { exportAll(false); }), 230, 44 });
    items.push_back({ addButton("Export all > save as...", false, [this] { exportAll(true); }), 230, 44 });
    items.push_back({ addButton("Import state / zip...", false, [this] { importStates(); }), 230, 44 });
    items.push_back({ addButton("WAV recordings...", false, [this] { listMode = 2; afterChange(); }), 230, 44 });
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
    section("Tap a state to load it  -  Share / Save as export that one state");
    if (stateMessage.isNotEmpty()) note(stateMessage, 22);
    auto files = statesDir().findChildFiles(juce::File::findFiles, false, "*.kr106state");
    files.sort();
    if (files.isEmpty())
      note("No saved states yet. Use \"Save current state...\" in the Options screen.", 24);
    for (auto& f : files)
    {
      auto label = f.getFileNameWithoutExtension() + "    " + f.getLastModificationTime().formatted("%d/%m %H:%M");
      items.push_back({ addButton(label, false, [this, f] { loadStateFile(f); }), 300, 44 });
      items.push_back({ addButton("Share", false, [this, f] { shareFile(f, [this](const juce::String& e) { setMessage(e.isEmpty() ? juce::String("Shared.") : e); }); }), 84, 44 });
      items.push_back({ addButton("Save as...", false, [this, f] { saveFileAs(f, f.getFileName(), "*.kr106state", [this](const juce::String& m) { setMessage(m); }); }), 110, 44 });
      const bool pending = (pendingDelete == f.getFullPathName());
      items.push_back({ addButton(pending ? "Confirm delete" : "Delete", pending, [this, f] {
                          if (pendingDelete == f.getFullPathName()) { f.deleteFile(); pendingDelete.clear(); }
                          else pendingDelete = f.getFullPathName();
                          afterChange(); }), 130, 44 });
    }
    items.push_back({ addButton("Back", false, [this] { listMode = 0; pendingDelete.clear(); afterChange(); }), 130, 44 });
    items.push_back({ addButton("Import...", false, [this] { importStates(); }), 130, 44 });
  }

  void buildRecordingList()
  {
    section("WAV recordings (24-bit stereo, what the speakers got)");
    if (stateMessage.isNotEmpty()) note(stateMessage, 22);
    auto files = recordingsDir().findChildFiles(juce::File::findFiles, false, "*.wav");
    files.sort();
    if (files.isEmpty())
      note("No recordings yet. Use the red button on the FX page.", 24);
    for (int i = files.size(); --i >= 0;)                           // newest first
    {
      auto f = files[i];
      auto label = f.getFileNameWithoutExtension().replace("KR106_", "") + "   " + juce::String(f.getSize() / 1048576.0, 1) + " MB";
      items.push_back({ addLabelBox(label), 300, 44 });
      items.push_back({ addButton("Share", false, [this, f] { shareFile(f, [this](const juce::String& e) { setMessage(e.isEmpty() ? juce::String("Shared.") : e); }); }), 84, 44 });
      items.push_back({ addButton("Save as...", false, [this, f] { saveFileAs(f, f.getFileName(), "*.wav", [this](const juce::String& m) { setMessage(m); }); }), 110, 44 });
      const bool pending = (pendingDelete == f.getFullPathName());
      items.push_back({ addButton(pending ? "Confirm delete" : "Delete", pending, [this, f] {
                          if (pendingDelete == f.getFullPathName()) { f.deleteFile(); pendingDelete.clear(); }
                          else pendingDelete = f.getFullPathName();
                          afterChange(); }), 130, 44 });
    }
    items.push_back({ addButton("Back", false, [this] { listMode = 0; pendingDelete.clear(); afterChange(); }), 130, 44 });
  }

  juce::Component* addLabelBox(const juce::String& text)
  {
    auto* b = new Btn(text, [] {}, juce::Colour(0xff1d1d1d));
    buttons.add(b);
    content.addAndMakeVisible(b);
    return b;
  }

  void setMessage(const juce::String& m)
  {
    stateMessage = m;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<KR106OptionsPanel>(this)] {
      if (safe) safe->rebuild(); });
  }

  // All saved states (+ the default one) in a single .zip: share it, or save it anywhere
  void exportAll(bool saveAs)
  {
    exportDir().createDirectory();
    auto zip = exportDir().getChildFile("KR106_states_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M") + ".zip");
    if (!kr106state::buildExportZip(statesDir(), defaultStateFile(), zip))
    {
      setMessage("Nothing to export yet: save a state first.");
      return;
    }
    if (saveAs) saveFileAs(zip, zip.getFileName(), "*.zip", [this](const juce::String& m) { setMessage(m); });
    else        shareFile(zip, [this](const juce::String& e) { setMessage(e.isEmpty() ? juce::String("Exported all states.") : e); });
  }

  // Bring state files (or a zip made by "Export all") back in as saved states
  void importStates()
  {
    auto count = std::make_shared<int>(0);
    auto bad = std::make_shared<int>(0);
    auto unread = std::make_shared<int>(0);
    auto seen = std::make_shared<int>(0);
    pickImport(
      [count, bad, unread, seen](const juce::String& name, const juce::MemoryBlock& mb, bool ok)
      {
        ++*seen;
        if (! ok) { ++*unread; return; }
        const int n = kr106state::importData(statesDir(), name, mb);
        if (n > 0) *count += n; else ++*bad;
      },
      [this, count, bad, unread, seen]
      {
        if (*seen == 0) { setMessage("No file was chosen."); return; }
        juce::String m = "Imported " + juce::String(*count) + " state(s)";
        if (*bad > 0)    m << ", " << juce::String(*bad) << " not a valid state";
        if (*unread > 0) m << ", " << juce::String(*unread) << " could not be read";
        setMessage(m + ".");
      });
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
    listMode = 0; pendingDelete.clear();
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
  int listMode = 0;                       // 0 = Options, 1 = saved states, 2 = recordings
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
// Sits between the audio device and the player: the player produces the sound, then the
// recorder copies the finished master output (after the limiter).
class KR106AudioTap : public juce::AudioIODeviceCallback
{
public:
  KR106AudioTap(juce::AudioProcessorPlayer& p, KR106Recorder& r) : player(p), rec(r) {}

  void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                        const juce::AudioIODeviceCallbackContext& ctx) override
  {
    player.audioDeviceIOCallbackWithContext(in, nIn, out, nOut, n, ctx);
    rec.write(out, nOut, n);
  }
  void audioDeviceAboutToStart(juce::AudioIODevice* d) override
  {
    rec.stop();                                   // a device restart (buffer / rate change) ends the take
    rec.setSampleRate(d->getCurrentSampleRate());
    player.audioDeviceAboutToStart(d);
  }
  void audioDeviceStopped() override { player.audioDeviceStopped(); }
  void audioDeviceError(const juce::String& e) override { player.audioDeviceError(e); }

private:
  juce::AudioProcessorPlayer& player;
  KR106Recorder& rec;
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
    deviceManager.addAudioCallback(&tap);
    deviceManager.addMidiInputDeviceCallback({}, forwarder.get());   // all enabled MIDI inputs

    // WAV recorder hooks for the FX page
    {
      auto& hr = kr106touch::hostRecorder();
      hr.toggle      = [this] { toggleRecording(); };
      hr.isRecording = [this] { return recorder.isRecording(); };
      hr.seconds     = [this] { return recorder.seconds(); };
      hr.level       = [this] { return recorder.takePeak(); };
      hr.lastName    = [this] { return lastTake.getFileName(); };
      hr.shareLast   = [this] { if (lastTake.existsAsFile()) shareFile(lastTake, [](const juce::String&) {}); };
      hr.saveLastAs  = [this] { if (lastTake.existsAsFile()) saveFileAs(lastTake, lastTake.getFileName(), "*.wav", [](const juce::String&) {}); };
    }

    editor.reset(processor->createEditor());
    addAndMakeVisible(*editor);

    kr106touch::audioMidiHook() = [this] { showAudioMidi(); };

    scanMidi();
    startTimer(1500);   // pick up controllers plugged in later
  }

  ~KR106Root() override
  {
    kr106touch::audioMidiHook() = nullptr;
    kr106touch::hostRecorder() = kr106touch::HostRecorder();
    recorder.stop();
    stopTimer();
    panel.reset();
    saveState();
    saveAudioSettings();
    deviceManager.removeMidiInputDeviceCallback({}, forwarder.get());
    deviceManager.removeAudioCallback(&tap);
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
    if (recorder.isRecording()) recorder.stop();       // closes the WAV properly before Android may kill us
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
    deviceManager.removeAudioCallback(&tap);
    player.setProcessor(nullptr);
    editor.reset();
    processor = std::make_unique<KR106AudioProcessor>();
    for (int i = kSettingVoices; i < kNumParams; i++)
      if (auto* prm = processor->getParam(i)) prm->setValueNotifyingHost(prm->getDefaultValue());
    processor->saveGlobalSettings();
    player.setProcessor(processor.get());
    deviceManager.addAudioCallback(&tap);
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

  void toggleRecording()
  {
    if (recorder.isRecording())
    {
      recorder.stop();
      lastTake = recorder.file();
      return;
    }
    auto f = recordingsDir().getChildFile("KR106_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".wav");
    if (recorder.start(f)) lastTake = f;
  }

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
  KR106Recorder recorder;
  KR106AudioTap tap { player, recorder };
  juce::File lastTake;
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
    // Android has no MIME type for our extension: map it, so pickers and share sheets treat it as a file
    juce::FileChooser::registerCustomMimeTypeForFileExtension("application/octet-stream", "kr106state");
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
