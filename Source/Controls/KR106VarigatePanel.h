#pragma once

// "Seq" page: a Varigate 8+ style step sequencer panel.
//
//   * 16 horizontal step sliders in two columns of 8 (steps 1-8 and 9-16). Every slider carries its own
//     LED in the handle: it shows the value / the gate of the step and flashes while the step plays.
//   * Page buttons choose what the sliders edit: GATE, PROB, RPT, DLY, PW, NOTE, GLIDE. TRACK turns the
//     sliders into the track settings (divide/multiply, length, direction, key, scale notes).
//   * The row of 10 buttons at the bottom selects the track: 8 gates and 2 CV tracks (CV 1, CV 2).
//     With MUTE on they mute / unmute the track, with CLEAR armed they clear it.
//   * BANK and PRESET rows: 10 banks of 10 presets. SAVE armed + preset button = save, otherwise recall.
//     SONG plays the preset list of the bank, S.PRG + preset buttons builds that list (CLEAR + S.PRG empties it).
//
// All data lives in the DSP (kr106::VarigateSeq); this panel only edits it and shows its state.

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../PluginProcessor.h"
#include "../DSP/KR106Varigate.h"
#include "../DSP/KR106Scale.h"

class KR106VarigatePanel : public juce::Component, private juce::Timer
{
public:
  explicit KR106VarigatePanel(KR106AudioProcessor& p) : mProc(p), mSeq(p.getSeq())
  {
    startTimerHz(30);
  }

  void resized() override { layout(); }

  void paint(juce::Graphics& g) override
  {
    using namespace kr106;
    // panel
    g.fillAll(juce::Colour(0xff161616));
    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawRect(getLocalBounds(), 1);

    const int pos = mSeq.mPos[mSel].load(std::memory_order_relaxed);
    const bool high = mSeq.mHigh[mSel].load(std::memory_order_relaxed) != 0;
    VgTrack& tr = mSeq.mPat.t[mSel];

    // ---- sliders ----
    for (int i = 0; i < kVgSteps; i++)
    {
      const auto trk = trackRect(i);
      float frac = 0.f, level = 0.f;
      juce::String text;
      stepInfo(i, frac, level, text);
      const bool playing = !mTrackMode && pos == i;
      if (playing) level = high ? 1.f : 0.72f;

      // step number
      g.setColour(juce::Colours::white.withAlpha(0.45f));
      g.setFont(juce::FontOptions(10.f));
      g.drawText(juce::String(i + 1), trk.getX() - 26, trk.getY(), 22, trk.getHeight(), juce::Justification::centredRight);

      // slot + ticks
      const float cy = static_cast<float>(trk.getCentreY());
      g.setColour(juce::Colour(0xff050505));
      g.fillRoundedRectangle(static_cast<float>(trk.getX()), cy - 3.f, static_cast<float>(trk.getWidth()), 6.f, 3.f);
      g.setColour(juce::Colour(0xff4a4a4a));
      for (int k = 0; k <= 4; k++)
        g.fillRect(trk.getX() + k * (trk.getWidth() - 1) / 4, trk.getBottom() - 4, 1, 4);

      // handle with its LED
      const int hw = kHandleW, hh = trk.getHeight() - 2;
      const float hx = trk.getX() + frac * static_cast<float>(trk.getWidth() - hw);
      const juce::Rectangle<float> h(hx, cy - hh * 0.5f, static_cast<float>(hw), static_cast<float>(hh));
      g.setColour(juce::Colour(0xffcfcfcf));
      g.fillRoundedRectangle(h, 4.f);
      g.setColour(juce::Colour(0xff7a7a7a));
      g.drawRoundedRectangle(h, 4.f, 1.f);
      g.setColour(juce::Colour(0xff9a9a9a));
      g.fillRect(h.getX() + 4.f, h.getY() + 3.f, 1.f, h.getHeight() - 6.f);          // grip lines
      g.fillRect(h.getRight() - 5.f, h.getY() + 3.f, 1.f, h.getHeight() - 6.f);
      drawLed(g, h.getCentreX(), h.getCentreY(), 5.f, level);

      // value text
      g.setColour(juce::Colour(0xffe8e8e8));
      g.setFont(juce::FontOptions(10.5f));
      g.drawText(text, trk.getRight() + 4, trk.getY(), kTextW, trk.getHeight(), juce::Justification::centredLeft);
    }

    // ---- buttons ----
    for (auto& b : mButtons)
    {
      bool lit = isLit(b.id);
      const bool disabled = (b.id == kLink && mSel < kVgGates);
      auto r = b.r.toFloat().reduced(1.f);
      g.setColour(lit ? juce::Colour(0xff6a1a14) : juce::Colour(0xff2a2a2a));
      g.fillRoundedRectangle(r, 4.f);
      g.setColour(juce::Colour(0xff7c7c7c).withAlpha(disabled ? 0.4f : 1.f));
      g.drawRoundedRectangle(r, 4.f, 1.f);
      // small LED in the corner of the buttons that are on/off
      if (b.id != kTempo)
        drawLed(g, r.getX() + 8.f, r.getY() + 8.f, 3.2f, lit ? 1.f : 0.f);
      g.setColour(juce::Colours::white.withAlpha(disabled ? 0.35f : 1.f));
      g.setFont(juce::FontOptions(b.id >= kBankBase ? 10.5f : 12.f));
      g.drawFittedText(buttonText(b.id), b.r.reduced(14, 1).withTrimmedLeft(2), juce::Justification::centred, 1, 0.7f);
    }

    // channel LEDs (gate activity) over the bottom buttons
    for (int t = 0; t < kVgTracks; t++)
    {
      const auto r = chanRect(t);
      const bool on = mSeq.mHigh[t].load(std::memory_order_relaxed) != 0;
      drawLed(g, r.getRight() - 10.f, r.getY() + 9.f, 4.f, on ? 1.f : (mSeq.mPat.t[t].mute.get() ? 0.f : 0.12f));
      if (mSeq.mPat.t[t].mute.get())
      {
        g.setColour(juce::Colour(0xffff5040));
        g.setFont(juce::FontOptions(9.f));
        g.drawText("MUTE", r.getX() + 2, r.getBottom() - 12, r.getWidth() - 4, 10, juce::Justification::centred);
      }
    }

    // ---- captions and info ----
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(juce::FontOptions(10.f));
    g.drawText("TRACK", 8, kChanY, 60, kChanH, juce::Justification::centredLeft);
    g.drawText("BANK",  8, kBankY, 60, kBankH, juce::Justification::centredLeft);
    g.drawText("PRESET", 8, kSlotY, 60, kBankH, juce::Justification::centredLeft);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(11.f));
    const juce::String trackName = mSel < kVgGates ? "GATE " + juce::String(mSel + 1) : "CV " + juce::String(mSel - kVgGates + 1);
    juce::String info = trackName;
    if (mTrackMode) info += "  -  TRACK SETTINGS";
    else info += juce::String("  -  ") + pageName(mPage);
    g.drawText(info, 704, 200, 158, 14, juce::Justification::centredLeft);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(juce::FontOptions(10.f));
    g.drawText("Bank " + juce::String(mSeq.mBank.load() + 1) + "  Preset " + juce::String(mSeq.mSlot.load() + 1),
               704, 216, 158, 12, juce::Justification::centredLeft);
    // song list of the bank
    {
      const int bank = mSeq.mBank.load();
      juce::String song = "Song:";
      for (int k = 0; k < 10; k++) { const int v = mSeq.mSong[bank][k].get(); if (v > 0) song += " " + juce::String(v); }
      if (song == "Song:") song += " (empty)";
      g.drawText(song, 704, 230, 158, 12, juce::Justification::centredLeft);
    }
    if (mTrackMode)
    {
      g.setColour(juce::Colours::white.withAlpha(0.7f));
      g.drawText("key " + juce::String(kNoteNames[mSeq.mPat.key.get() % 12]) + "   dir " + kVgDirNames[std::min<int>(3, tr.dir.get())],
                 704, 244, 158, 12, juce::Justification::centredLeft);
    }
    else if (mSel >= kVgGates)
    {
      const int l = tr.link.get();
      g.setColour(juce::Colours::white.withAlpha(0.7f));
      g.drawText(l == 0 ? juce::String("independent clock") : "follows gate " + juce::String(l), 704, 244, 158, 12, juce::Justification::centredLeft);
    }
  }

  void mouseDown(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    mPressed = -1;
    for (auto& b : mButtons)
      if (b.r.contains(p)) { mPressed = b.id; break; }
    if (mPressed == kTempo) { mTempoStart = mSeq.mBpm.load(); mTempoX = p.x; return; }
    if (mPressed >= 0) { repaint(); return; }

    const int step = stepAt(p);
    if (step < 0) return;
    mDragging = true;
    mLastStep = -1;
    beginSlider(step, p);
    mLastStep = step;
  }

  void mouseDrag(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    if (mPressed == kTempo)
    {
      const float bpm = mTempoStart + static_cast<float>(p.x - mTempoX) * 0.5f;
      mSeq.mBpm.store(juce::jlimit(30.f, 300.f, std::round(bpm)));
      repaint();
      return;
    }
    if (!mDragging) return;
    const int step = stepAt(p, true);
    if (step < 0) return;
    if (step != mLastStep) { mLastStep = step; dragSlider(step, p, true); }
    else dragSlider(step, p, false);
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    if (mPressed >= 0 && mPressed != kTempo)
    {
      bool inside = false;
      for (auto& b : mButtons) if (b.id == mPressed && b.r.contains(p)) inside = true;
      if (inside) press(mPressed);
    }
    mPressed = -1;
    mDragging = false;
    repaint();
  }

private:
  // ---- layout constants (panel coordinates) ----
  static constexpr int kColX0 = 200, kColX1 = 466, kTrackW = 200, kRowY0 = 10, kRowH = 31, kHandleW = 26, kTextW = 34;
  static constexpr int kChanY = 268, kChanH = 32, kBankY = 306, kBankH = 22, kSlotY = 332;

  enum Id {
    kPageGate = 0, kPageProb, kPageRpt, kPageDly, kPagePw, kPageNote, kPageGlide,   // 0..6 = VgPage
    kTrack = 10, kMute, kClear, kSave, kSong, kSongPrg, kLink,
    kRun = 20, kReset, kSync, kFreeze, kRnd, kRndAll, kRotL, kRotR, kTempo,
    kChanBase = 100,         // 100..109
    kBankBase = 120,         // 120..129
    kSlotBase = 140          // 140..149
  };
  struct Btn { juce::Rectangle<int> r; int id; };

  KR106AudioProcessor& mProc;
  kr106::VarigateSeq& mSeq;
  std::vector<Btn> mButtons;
  int mSel = 0, mPage = 0;
  bool mTrackMode = false, mMuteMode = false, mClearArm = false, mSaveArm = false, mSongPrg = false;
  int mPressed = -1, mLastStep = -1;
  bool mDragging = false;
  int mSetTo = 0;                               // gate / toggle value being painted by a drag
  float mTempoStart = 120.f;
  int mTempoX = 0;

  void timerCallback() override { if (isShowing()) repaint(); }

  static const char* pageName(int p)
  {
    static const char* n[7] = { "GATE", "PROBABILITY", "REPEAT", "DELAY", "PULSEWIDTH", "NOTE", "GLIDE" };
    return n[juce::jlimit(0, 6, p)];
  }

  void layout()
  {
    mButtons.clear();
    const int lx0 = 8, lx1 = 92, bw = 78, bh = 30, ly = 10, lstep = 34;
    const int left[7][2] = { { kPageGate, kPageProb }, { kPageRpt, kPageDly }, { kPagePw, kPageNote },
                             { kPageGlide, kTrack }, { kMute, kClear }, { kSave, kSong }, { kSongPrg, kLink } };
    for (int r = 0; r < 7; r++)
    {
      mButtons.push_back({ { lx0, ly + r * lstep, bw, bh }, left[r][0] });
      mButtons.push_back({ { lx1, ly + r * lstep, bw, bh }, left[r][1] });
    }
    const int rx0 = 704, rx1 = 784, rw = 78;
    mButtons.push_back({ { rx0, 10, 158, 34 }, kTempo });
    const int right[4][2] = { { kRun, kReset }, { kSync, kFreeze }, { kRnd, kRndAll }, { kRotL, kRotR } };
    for (int r = 0; r < 4; r++)
    {
      mButtons.push_back({ { rx0, 52 + r * 36, rw, 30 }, right[r][0] });
      mButtons.push_back({ { rx1, 52 + r * 36, rw, 30 }, right[r][1] });
    }
    for (int i = 0; i < 10; i++)
    {
      mButtons.push_back({ chanRect(i), kChanBase + i });
      mButtons.push_back({ { 200 + i * 66 - 28, kBankY, 62, kBankH }, kBankBase + i });
      mButtons.push_back({ { 200 + i * 66 - 28, kSlotY, 62, kBankH }, kSlotBase + i });
    }
  }

  juce::Rectangle<int> chanRect(int t) const { return { 200 + t * 66 - 28, kChanY, 62, kChanH }; }

  juce::Rectangle<int> trackRect(int step) const
  {
    const int col = step / 8, row = step % 8;
    return { (col == 0 ? kColX0 : kColX1), kRowY0 + row * kRowH + 3, kTrackW, kRowH - 6 };
  }

  // which step slider is at this point (outside the sliders: -1). While dragging, any point in the
  // slider zone counts, so a drag can sweep over several steps.
  int stepAt(juce::Point<int> p, bool loose = false) const
  {
    if (p.y < kRowY0 || p.y >= kRowY0 + 8 * kRowH) return -1;
    const int row = (p.y - kRowY0) / kRowH;
    int col;
    if (p.x >= kColX0 - 28 && p.x < kColX0 + kTrackW + kTextW + 8) col = 0;
    else if (p.x >= kColX1 - 28 && p.x < kColX1 + kTrackW + kTextW + 8) col = 1;
    else if (loose) col = (p.x < (kColX0 + kTrackW + kColX1) / 2) ? 0 : 1;
    else return -1;
    return col * 8 + row;
  }

  float fracAt(int step, juce::Point<int> p) const
  {
    const auto t = trackRect(step);
    return juce::jlimit(0.f, 1.f, static_cast<float>(p.x - t.getX() - kHandleW / 2) / static_cast<float>(t.getWidth() - kHandleW));
  }

  static void drawLed(juce::Graphics& g, float cx, float cy, float rad, float level)
  {
    level = juce::jlimit(0.f, 1.f, level);
    if (level > 0.5f)
    {
      g.setColour(juce::Colour(0xffff4020).withAlpha(0.25f * (level - 0.5f) * 2.f));
      g.fillEllipse(cx - rad * 1.9f, cy - rad * 1.9f, rad * 3.8f, rad * 3.8f);
    }
    g.setColour(juce::Colour(0xff3a0c08).interpolatedWith(juce::Colour(0xffff4a28), level));
    g.fillEllipse(cx - rad, cy - rad, rad * 2.f, rad * 2.f);
    g.setColour(juce::Colour(0xff000000).withAlpha(0.6f));
    g.drawEllipse(cx - rad, cy - rad, rad * 2.f, rad * 2.f, 0.8f);
  }

  static juce::String noteName(int pitch)
  {
    return juce::String(kr106::kNoteNames[((pitch % 12) + 12) % 12]) + juce::String(pitch / 12 - 1);
  }

  // handle position (0..1), LED level (0..1) and the text for one slider
  void stepInfo(int i, float& frac, float& level, juce::String& text) const
  {
    using namespace kr106;
    const VgTrack& t = mSeq.mPat.t[mSel];
    if (mTrackMode)
    {
      if (i == 0)      { const int d = std::min<int>(kVgNumDiv - 1, t.div.get()); frac = d / 12.f; text = kVgDivNames[d]; }
      else if (i == 1) { const int l = juce::jlimit(1, 16, static_cast<int>(t.len.get())); frac = (l - 1) / 15.f; text = juce::String(l); }
      else if (i == 2) { const int d = std::min<int>(3, t.dir.get()); frac = d / 3.f; text = juce::String(kVgDirNames[d]).substring(0, 4); }
      else if (i == 3) { const int k = mSeq.mPat.key.get() % 12; frac = k / 11.f; text = kNoteNames[k]; }
      else
      {
        const bool on = (mSeq.mPat.scale() >> (i - 4)) & 1;
        frac = on ? 1.f : 0.f; text = juce::String("+") + juce::String(i - 4);
        level = on ? 0.85f : 0.08f;
        return;
      }
      level = 0.55f;
      return;
    }
    const int v = const_cast<VgTrack&>(t).pageArray(mPage)[i].get();
    frac = v / 127.f;
    switch (mPage)
    {
      case kVgPgGate:  frac = v >= 64 ? 1.f : 0.f; level = v >= 64 ? 0.5f : 0.05f; text = v >= 64 ? "ON" : ""; break;
      case kVgPgProb:  level = 0.12f + 0.55f * frac; text = juce::String(juce::roundToInt(v / 127.f * 100.f)) + "%"; break;
      case kVgPgRpt:   level = 0.12f + 0.55f * frac; text = "x" + juce::String(1 + std::min(7, v / 16)); break;
      case kVgPgDly:   level = 0.12f + 0.55f * frac; text = juce::String(juce::roundToInt(v / 127.f * 90.f)) + "%"; break;
      case kVgPgPw:    level = 0.12f + 0.55f * frac; text = juce::String(juce::roundToInt((0.05f + 0.95f * v / 127.f) * 100.f)) + "%"; break;
      case kVgPgNote:  level = 0.12f + 0.55f * frac; text = noteName(vgNoteFor(mSeq.mPat, v)); break;
      default:         level = 0.12f + 0.55f * frac; text = v == 0 ? "" : juce::String(juce::roundToInt(v / 127.f * 100.f)) + "%"; break;
    }
  }

  void beginSlider(int step, juce::Point<int> p)
  {
    using namespace kr106;
    VgTrack& t = mSeq.mPat.t[mSel];
    const float f = fracAt(step, p);
    if (mTrackMode)
    {
      if (step >= 4)
      {
        const uint16_t m = mSeq.mPat.scale();
        const bool on = (m >> (step - 4)) & 1;
        mSetTo = on ? 0 : 1;
        setScaleBit(step - 4, mSetTo != 0);
      }
      else dragSlider(step, p, true);
      return;
    }
    if (mPage == kVgPgGate)
    {
      mSetTo = t.gate[step].get() >= 64 ? 0 : 127;
      t.gate[step].set(mSetTo);
    }
    else t.pageArray(mPage)[step].set(juce::roundToInt(f * 127.f));
  }

  void dragSlider(int step, juce::Point<int> p, bool newStep)
  {
    using namespace kr106;
    VgTrack& t = mSeq.mPat.t[mSel];
    const float f = fracAt(step, p);
    if (mTrackMode)
    {
      if (step >= 4) { if (newStep) setScaleBit(step - 4, mSetTo != 0); return; }
      if (step == 0)      t.div.set(juce::roundToInt(f * 12.f));
      else if (step == 1) t.len.set(1 + juce::roundToInt(f * 15.f));
      else if (step == 2) t.dir.set(juce::roundToInt(f * 3.f));
      else                mSeq.mPat.key.set(juce::roundToInt(f * 11.f));
      return;
    }
    if (mPage == kVgPgGate) { if (newStep) t.gate[step].set(mSetTo); }      // painting on / off
    else t.pageArray(mPage)[step].set(juce::roundToInt(f * 127.f));
  }

  void setScaleBit(int bit, bool on)
  {
    uint16_t m = mSeq.mPat.scale();
    if (on) m = static_cast<uint16_t>(m | (1u << bit)); else m = static_cast<uint16_t>(m & ~(1u << bit));
    if ((m & 0x0FFF) == 0) m = static_cast<uint16_t>(1u << bit);          // never an empty scale
    mSeq.mPat.setScale(m);
  }

  bool isLit(int id) const
  {
    if (id >= kPageGate && id <= kPageGlide) return !mTrackMode && mPage == id;
    if (id >= kChanBase && id < kChanBase + 10) return mSel == id - kChanBase;
    if (id >= kBankBase && id < kBankBase + 10) return mViewBank() == id - kBankBase;
    if (id >= kSlotBase && id < kSlotBase + 10) return mViewBank() == mSeq.mBank.load() && mSeq.mSlot.load() == id - kSlotBase;
    switch (id)
    {
      case kTrack: return mTrackMode;   case kMute: return mMuteMode;   case kClear: return mClearArm;
      case kSave: return mSaveArm;      case kSong: return mSeq.mSongOn.load();   case kSongPrg: return mSongPrg;
      case kRun: return mSeq.mRun.load();   case kSync: return mSeq.mSync.load();   case kFreeze: return mSeq.mFreeze.load();
      default: return false;
    }
  }
  int mViewBank() const { return mSeq.mBank.load(); }

  juce::String buttonText(int id) const
  {
    using namespace kr106;
    if (id >= kChanBase && id < kChanBase + 10)
    {
      const int t = id - kChanBase;
      return t < kVgGates ? juce::String(t + 1) : "CV " + juce::String(t - kVgGates + 1);
    }
    if (id >= kBankBase && id < kBankBase + 10) return juce::String(id - kBankBase + 1);
    if (id >= kSlotBase && id < kSlotBase + 10) return juce::String(id - kSlotBase + 1);
    switch (id)
    {
      case kPageGate: return "GATE";  case kPageProb: return "PROB";  case kPageRpt: return "RPT";
      case kPageDly:  return "DLY";   case kPagePw:   return "PW";    case kPageNote: return "NOTE";
      case kPageGlide: return "GLIDE";
      case kTrack: return "TRACK";    case kMute: return "MUTE";      case kClear: return "CLEAR";
      case kSave: return "SAVE";      case kSong: return "SONG";      case kSongPrg: return "S.PRG";
      case kLink:
      {
        if (mSel < kVgGates) return "LINK";
        const int l = mSeq.mPat.t[mSel].link.get();
        return l == 0 ? juce::String("LINK -") : "LINK G" + juce::String(l);
      }
      case kRun: return mSeq.mRun.load() ? "STOP" : "RUN";
      case kReset: return "RESET";    case kSync: return "SYNC";      case kFreeze: return "FREEZE";
      case kRnd: return "RND";        case kRndAll: return "RND ALL"; case kRotL: return "ROT <"; case kRotR: return "ROT >";
      case kTempo:
        return mSeq.mSync.load() ? juce::String("TEMPO  (host sync)") : "TEMPO  " + juce::String(juce::roundToInt(mSeq.mBpm.load()));
      default: return {};
    }
  }

  void press(int id)
  {
    using namespace kr106;
    if (id >= kPageGate && id <= kPageGlide) { mPage = id; mTrackMode = false; return; }
    if (id >= kChanBase && id < kChanBase + 10)
    {
      const int t = id - kChanBase;
      if (mClearArm) { mSeq.ClearTrack(t); mClearArm = false; }
      else if (mMuteMode) mSeq.mPat.t[t].mute.set(mSeq.mPat.t[t].mute.get() ? 0 : 1);
      else mSel = t;
      return;
    }
    if (id >= kBankBase && id < kBankBase + 10) { mSeq.mBank.store(id - kBankBase); return; }
    if (id >= kSlotBase && id < kSlotBase + 10)
    {
      const int bank = mSeq.mBank.load(), slot = id - kSlotBase;
      if (mSaveArm) { mSeq.Save(bank * 10 + slot); mSaveArm = false; mSeq.mSlot.store(slot); }
      else if (mSongPrg)
      {
        int freeIdx = -1;
        for (int k = 0; k < 10; k++) if (mSeq.mSong[bank][k].get() == 0) { freeIdx = k; break; }
        if (freeIdx >= 0) mSeq.mSong[bank][freeIdx].set(slot + 1);
      }
      else mSeq.Recall(bank * 10 + slot);
      return;
    }
    switch (id)
    {
      case kTrack: mTrackMode = !mTrackMode; break;
      case kMute:  mMuteMode = !mMuteMode; break;
      case kClear:
        if (mSongPrg) { for (int k = 0; k < 10; k++) mSeq.mSong[mSeq.mBank.load()][k].set(0); mClearArm = false; }
        else mClearArm = !mClearArm;
        break;
      case kSave:  mSaveArm = !mSaveArm; break;
      case kSong:  mSeq.mSongOn.store(!mSeq.mSongOn.load()); break;
      case kSongPrg: mSongPrg = !mSongPrg; break;
      case kLink:
        if (mSel >= kVgGates) mSeq.mPat.t[mSel].link.set((mSeq.mPat.t[mSel].link.get() + 1) % (kVgGates + 1));
        break;
      case kRun:   mSeq.mRun.store(!mSeq.mRun.load()); break;
      case kReset: mSeq.RequestReset(); break;
      case kSync:  mSeq.mSync.store(!mSeq.mSync.load()); break;
      case kFreeze: mSeq.mFreeze.store(!mSeq.mFreeze.load()); break;
      case kRnd:   if (mTrackMode) mSeq.RandomizeAll(mSel); else mSeq.Randomize(mSel, mPage); break;
      case kRndAll: mSeq.RandomizeAll(mSel); break;
      case kRotL:  mSeq.Rotate(mSel, -1); break;
      case kRotR:  mSeq.Rotate(mSel, +1); break;
      default: break;
    }
  }
};
