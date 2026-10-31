#pragma once

// "Seq" page: a step sequencer panel in the style of the Malekko Varigate 8+.
//
//   * Two columns of 8 sliders (steps 1-8 and 9-16). Each slider has a black cap with a big rectangular
//     red LED, and a white strip under it with its name and value: "STEP 5  PROB 80%", or, with TRACK
//     on, the track setting the slider controls (DIVIDE/MULTIPLY, SEQ LENGTH, SEQ MODE, KEY, ROOT ...).
//   * Grey round buttons that light up when active: PROB RPT DLY / PW NOTE GLIDE / TRACK MUTE CLEAR /
//     SONG TEMPO RUN / BANK SAVE RECALL, and RND, FREEZE and END in the jack row. Tapping the lit page
//     button again goes back to the GATE page.
//   * Left: CLK IN (SYNC with the host), RESET, CLK OUT with its LED, and the 12-LED note / value meter
//     with the scale keys next to it.
//   * Small buttons: LINK, ROT <, ROT >, R.ALL (randomize everything), S.PRG, ALL (the next slider moves set every
//     step), REC (live recording: played keys, or TAP, write the gate and the note into the step that plays),
//     TAP and G+D (GATE+DELAY: a step with a delay also plays a short pulse on the beat). In TRACK mode KEY and
//     ROOT + n set the scale of the BANK: every preset of the bank plays in it.
//   * Bottom row: GATE 1-8, CV 1, CV 2 (the red LED above each shows its gate). They select the track;
//     after BANK, SAVE or RECALL they pick the bank / preset (1-10) instead.
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
    g.fillAll(juce::Colour(0xff0b0b0b));
    g.setColour(juce::Colour(0xff262626));
    g.drawRect(getLocalBounds(), 1);

    const int pos = mSeq.mPos[mSel].load(std::memory_order_relaxed);
    const bool high = mSeq.mHigh[mSel].load(std::memory_order_relaxed) != 0;

    drawLeftStrip(g, pos);

    // ---- sliders: slot, white strip, black cap with a big rectangular LED ----
    for (int i = 0; i < kVgSteps; i++)
    {
      const auto trk = trackRect(i);
      const auto strip = stripRect(i);
      float frac = 0.f, level = 0.f;
      juce::String value;
      stepInfo(i, frac, level, value);
      if (!mTrackMode && pos == i) level = high ? 1.f : 0.75f;

      g.setColour(juce::Colour(0xff151515));
      g.fillRoundedRectangle(trk.toFloat(), 3.f);
      g.setColour(juce::Colour(0xff343434));
      g.drawRoundedRectangle(trk.toFloat().reduced(0.5f), 3.f, 1.f);
      g.setColour(juce::Colour(0xff2a2a2a));
      g.fillRect(trk.getX() + 6, trk.getCentreY(), trk.getWidth() - 12, 1);

      g.setColour(juce::Colour(0xffe7e7e7));
      g.fillRect(strip);
      g.setColour(juce::Colour(0xff111111));
      g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
      g.drawFittedText(stripText(i, value), strip.reduced(4, 0), juce::Justification::centred, 1, 0.8f);

      const float hx = trk.getX() + frac * static_cast<float>(trk.getWidth() - kHandleW);
      const juce::Rectangle<float> cap(hx, trk.getCentreY() - kHandleH * 0.5f, static_cast<float>(kHandleW), static_cast<float>(kHandleH));
      g.setColour(juce::Colour(0xff060606));
      g.fillRoundedRectangle(cap, 3.f);
      g.setColour(juce::Colour(0xff4c4c4c));
      g.drawRoundedRectangle(cap.reduced(0.5f), 3.f, 1.f);
      drawRectLed(g, juce::Rectangle<float>(0.f, 0.f, static_cast<float>(kLedW), static_cast<float>(kLedH)).withCentre(cap.getCentre()), level);
    }

    // ---- buttons, jacks, channel row ----
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    for (auto& it : mItems)
    {
      const auto c = it.r.getCentre().toFloat();
      const bool lit = isLit(it.id, now);
      g.setColour(juce::Colour(0xffdadada));
      g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
      switch (it.shape)
      {
        case Round:
          drawRoundButton(g, c.x, c.y, 15.f, lit);
          if (it.id == kRun) drawPlayGlyph(g, c, mSeq.mRun.load());
          g.drawText(buttonLabel(it), it.r.getX() - 22, it.r.getBottom() + 1, it.r.getWidth() + 44, 11, juce::Justification::centred);
          break;
        case Knob:
          drawKnob(g, c.x, c.y, 14.f, mSeq.mBpm.load(), mSeq.mSync.load());
          g.drawText(buttonLabel(it), it.r.getX() - 22, it.r.getBottom() + 1, it.r.getWidth() + 44, 11, juce::Justification::centred);
          break;
        case Jack:
          drawJack(g, c.x, c.y, 11.f, lit);
          if (it.id == kEnd) drawLed(g, c.x + 17.f, c.y - 11.f, 3.6f, mSeq.mEndOut.load() ? 1.f : 0.f);
          g.drawText(buttonLabel(it), it.r.getX() - 22, it.r.getBottom() + 1, it.r.getWidth() + 44, 11, juce::Justification::centred);
          break;
        case Mini:
        {
          const auto r = it.r.toFloat().reduced(1.f);
          g.setColour(lit ? juce::Colour(0xfff0f4fb) : juce::Colour(0xff8d8f94));
          g.fillRoundedRectangle(r, 4.f);
          g.setColour(juce::Colour(0xff2a2a2a));
          g.drawRoundedRectangle(r, 4.f, 1.f);
          g.setColour(juce::Colour(0xff101010).withAlpha(it.id == kLink && mSel < kVgGates ? 0.4f : 1.f));
          g.drawFittedText(buttonLabel(it), it.r.reduced(2, 0), juce::Justification::centred, 1, 0.8f);
          break;
        }
        case Chan:
        {
          const int t = it.id - kChanBase;
          drawRoundButton(g, c.x, c.y, 15.f, lit);
          const bool muted = mSeq.mPat.t[t].mute.get() != 0;
          const bool gate = mSeq.mHigh[t].load(std::memory_order_relaxed) != 0;
          drawLed(g, c.x, it.r.getY() - 9.f, 4.f, gate ? 1.f : (muted ? 0.f : 0.1f));
          g.setColour(muted ? juce::Colour(0xffff6a58) : juce::Colour(0xffdadada));
          g.drawText(muted ? juce::String("MUTED") : buttonLabel(it), it.r.getX() - 14, it.r.getBottom() + 1, it.r.getWidth() + 28, 11, juce::Justification::centred);
          break;
        }
      }
    }

    // ---- info line ----
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.setFont(juce::FontOptions(10.5f));
    g.drawText(infoLine(), 134, kInfoY, 512, 14, juce::Justification::centredLeft);
  }

  void mouseDown(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    mPressed = -1;
    for (auto& it : mItems)
      if (it.r.contains(p)) { mPressed = it.id; break; }
    if (mPressed == kTempo) { mTempoStart = mSeq.mBpm.load(); mTempoX = p.x; mTempoY = p.y; return; }
    if (mPressed >= 0) { repaint(); return; }

    const int step = stepAt(p, false);
    if (step < 0) return;
    mDragging = true;
    beginSlider(step, p);
    mLastStep = step;
  }

  void mouseDrag(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    if (mPressed == kTempo)
    {
      const float bpm = mTempoStart + static_cast<float>((p.x - mTempoX) + (mTempoY - p.y)) * 0.5f;
      mSeq.mBpm.store(juce::jlimit(30.f, 300.f, std::round(bpm)));
      return;
    }
    if (!mDragging) return;
    const int step = stepAt(p, true);
    if (step < 0) return;
    const bool changed = step != mLastStep;
    mLastStep = step;
    dragSlider(step, p, changed);
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    if (mPressed >= 0 && mPressed != kTempo)
    {
      bool inside = false;
      for (auto& it : mItems) if (it.id == mPressed && it.r.contains(p)) inside = true;
      if (inside) press(mPressed);
    }
    mPressed = -1;
    if (mDragging) mAllSteps = false;                       // ALL lasts for one slider move, like holding the button
    mDragging = false;
    mLastStep = -1;
    repaint();
  }

private:
  // ---- geometry (panel coordinates) ----
  static constexpr int kSlotX0 = 134, kSlotX1 = 396, kTrackW = 250, kTrackH = 14, kRowY0 = 6, kRowP = 31;
  static constexpr int kHandleW = 44, kHandleH = 17, kLedW = 32, kLedH = 11;
  static constexpr int kChanCy = 292, kInfoY = 336;
  static constexpr int kBarX = 28, kBarW = 46, kKeyX = 82, kKeyW = 22, kBarY0 = 140, kBarP = 9, kBarH = 6;

  enum Id {
    kPageGate = 0, kPageProb, kPageRpt, kPageDly, kPagePw, kPageNote, kPageGlide,      // 0..6 = VgPage
    kTrack = 10, kMute, kClear, kSong, kSongPrg, kLink, kBank, kSave, kRecall, kAllSteps, kRec, kTap, kGateDly,
    kRun = 30, kReset, kClkIn, kFreeze, kRnd, kRndAll, kRotL, kRotR, kTempo, kEnd, kClkOut,
    kChanBase = 100                                                                    // 100..109
  };
  enum Shape { Round, Mini, Jack, Knob, Chan };
  enum Arm { kArmNone = 0, kArmBank, kArmSave, kArmRecall };
  struct Item { int id; Shape shape; juce::Rectangle<int> r; juce::String label; };

  KR106AudioProcessor& mProc;
  kr106::VarigateSeq& mSeq;
  std::vector<Item> mItems;
  int mSel = 0, mPage = 0, mArm = kArmNone;
  bool mTrackMode = false, mMuteMode = false, mClearArm = false, mSongPrg = false, mDragging = false, mAllSteps = false;
  int mPressed = -1, mLastStep = -1, mSetTo = 0;
  float mTempoStart = 120.f;
  int mTempoX = 0, mTempoY = 0;
  // what the note / value meter shows after the last slider touch
  int mBarKind = 0, mBarVal = 0;                // kind 0: bar of mBarVal LEDs, kind 1: one LED (pitch class)
  juce::uint32 mBarUntil = 0, mRndFlash = 0;

  void timerCallback() override { if (isShowing()) repaint(); }

  void addItem(int id, Shape shape, int cx, int cy, int size, const juce::String& label)
  {
    mItems.push_back({ id, shape, juce::Rectangle<int>(size, size).withCentre({ cx, cy }), label });
  }

  void layout()
  {
    mItems.clear();
    // left jacks
    addItem(kClkIn, Jack, 34, 20, 30, "CLK IN");
    addItem(kReset, Jack, 34, 64, 30, "RESET");
    // button grid
    const int cx[3] = { 690, 768, 846 };
    const int cy[6] = { 22, 65, 108, 151, 194, 237 };
    addItem(kPageProb, Round, cx[0], cy[0], 34, "PROB");  addItem(kPageRpt, Round, cx[1], cy[0], 34, "RPT");
    addItem(kPageDly, Round, cx[2], cy[0], 34, "DLY");
    addItem(kPagePw, Round, cx[0], cy[1], 34, "PW");      addItem(kPageNote, Round, cx[1], cy[1], 34, "NOTE");
    addItem(kPageGlide, Round, cx[2], cy[1], 34, "GLIDE");
    addItem(kTrack, Round, cx[0], cy[2], 34, "TRACK");    addItem(kMute, Round, cx[1], cy[2], 34, "MUTE");
    addItem(kClear, Round, cx[2], cy[2], 34, "CLEAR");
    addItem(kSong, Round, cx[0], cy[3], 34, "SONG");      addItem(kTempo, Knob, cx[1], cy[3], 34, "TEMPO");
    addItem(kRun, Round, cx[2], cy[3], 34, "RUN/STOP");
    addItem(kBank, Round, cx[0], cy[4], 34, "BANK");      addItem(kSave, Round, cx[1], cy[4], 34, "SAVE");
    addItem(kRecall, Round, cx[2], cy[4], 34, "RECALL");
    addItem(kRnd, Jack, cx[0], cy[5], 30, "RND");         addItem(kFreeze, Jack, cx[1], cy[5], 30, "FREEZE");
    addItem(kEnd, Jack, cx[2], cy[5], 30, "END");
    // extra functions (small buttons, bottom left)
    const int mx[3] = { 4, 42, 80 }, my[3] = { 262, 285, 308 };
    auto mini = [&](int id, int col, int row, const char* label) {
      mItems.push_back({ id, Mini, juce::Rectangle<int>(mx[col], my[row], 36, 21), label });
    };
    mini(kLink, 0, 0, "LINK");     mini(kRotL, 1, 0, "ROT<");     mini(kRotR, 2, 0, "ROT>");
    mini(kRndAll, 0, 1, "R.ALL");  mini(kSongPrg, 1, 1, "S.PRG"); mini(kAllSteps, 2, 1, "ALL");
    mini(kRec, 0, 2, "REC");       mini(kTap, 1, 2, "TAP");       mini(kGateDly, 2, 2, "G+D");
    // track row
    for (int t = 0; t < kr106::kVgTracks; t++)
      addItem(kChanBase + t, Chan, 160 + t * 72, kChanCy, 34,
              t < kr106::kVgGates ? "GATE " + juce::String(t + 1) : "CV " + juce::String(t - kr106::kVgGates + 1));
  }

  juce::Rectangle<int> trackRect(int step) const
  {
    const int col = step / 8, row = step % 8;
    return { col == 0 ? kSlotX0 : kSlotX1, kRowY0 + row * kRowP, kTrackW, kTrackH };
  }
  juce::Rectangle<int> stripRect(int step) const
  {
    const auto t = trackRect(step);
    return { t.getX(), t.getBottom() + 2, t.getWidth(), 12 };
  }

  // the slider at this point; while dragging, anything in the slider rows counts so a drag can sweep steps
  int stepAt(juce::Point<int> p, bool loose) const
  {
    if (p.y < kRowY0 || p.y >= kRowY0 + 8 * kRowP) return -1;
    const int row = (p.y - kRowY0) / kRowP;
    int col;
    if (p.x >= kSlotX0 - 6 && p.x < kSlotX0 + kTrackW + 6) col = 0;
    else if (p.x >= kSlotX1 - 6 && p.x < kSlotX1 + kTrackW + 6) col = 1;
    else if (loose) col = (p.x < (kSlotX0 + kTrackW + kSlotX1) / 2) ? 0 : 1;
    else return -1;
    return col * 8 + row;
  }

  float fracAt(int step, juce::Point<int> p) const
  {
    const auto t = trackRect(step);
    return juce::jlimit(0.f, 1.f, static_cast<float>(p.x - t.getX() - kHandleW / 2) / static_cast<float>(t.getWidth() - kHandleW));
  }

  // ---- drawing helpers ----
  static void drawLed(juce::Graphics& g, float cx, float cy, float rad, float level)
  {
    level = juce::jlimit(0.f, 1.f, level);
    if (level > 0.5f)
    {
      g.setColour(juce::Colour(0xffff4020).withAlpha(0.28f * (level - 0.5f) * 2.f));
      g.fillEllipse(cx - rad * 2.f, cy - rad * 2.f, rad * 4.f, rad * 4.f);
    }
    g.setColour(juce::Colour(0xff340a06).interpolatedWith(juce::Colour(0xffff4a28), level));
    g.fillEllipse(cx - rad, cy - rad, rad * 2.f, rad * 2.f);
    g.setColour(juce::Colour(0xff000000).withAlpha(0.6f));
    g.drawEllipse(cx - rad, cy - rad, rad * 2.f, rad * 2.f, 0.8f);
  }

  // the big rectangular LED in the slider caps
  static void drawRectLed(juce::Graphics& g, juce::Rectangle<float> r, float level)
  {
    level = juce::jlimit(0.f, 1.f, level);
    if (level > 0.4f)
    {
      g.setColour(juce::Colour(0xffff3a1c).withAlpha(0.30f * (level - 0.4f) / 0.6f));
      g.fillRoundedRectangle(r.expanded(3.f, 2.5f), 4.f);
    }
    g.setColour(juce::Colour(0xff2c0805).interpolatedWith(juce::Colour(0xffff3a1c), level));
    g.fillRoundedRectangle(r, 2.f);
    g.setColour(juce::Colour(0xffffd0c0).withAlpha(0.45f * level));
    g.fillRoundedRectangle(r.reduced(r.getWidth() * 0.2f, r.getHeight() * 0.3f), 1.5f);
    g.setColour(juce::Colour(0xff000000).withAlpha(0.7f));
    g.drawRoundedRectangle(r, 2.f, 1.f);
  }

  // grey translucent caps of the original, white-blue when lit
  static void drawRoundButton(juce::Graphics& g, float cx, float cy, float r, bool lit)
  {
    if (lit)
    {
      g.setColour(juce::Colour(0xff9ec8ff).withAlpha(0.16f));
      g.fillEllipse(cx - r * 2.1f, cy - r * 2.1f, r * 4.2f, r * 4.2f);
      g.setColour(juce::Colour(0xffb4d4ff).withAlpha(0.26f));
      g.fillEllipse(cx - r * 1.55f, cy - r * 1.55f, r * 3.1f, r * 3.1f);
    }
    g.setColour(juce::Colour(0xff030303));
    g.fillEllipse(cx - r - 2.f, cy - r - 2.f, (r + 2.f) * 2.f, (r + 2.f) * 2.f);
    g.setColour(lit ? juce::Colour(0xfff1f6ff) : juce::Colour(0xff9fa1a6));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(lit ? juce::Colour(0xffffffff) : juce::Colour(0xffc6c8cc));
    g.fillEllipse(cx - r * 0.72f, cy - r * 0.8f, r * 1.44f, r * 1.2f);
    g.setColour(juce::Colour(0xff55575c).withAlpha(lit ? 0.35f : 0.8f));
    g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.f);
  }

  static void drawPlayGlyph(juce::Graphics& g, juce::Point<float> c, bool running)
  {
    g.setColour(juce::Colour(0xff2a2a2a));
    if (running) g.fillRect(c.x - 5.f, c.y - 5.f, 10.f, 10.f);
    else
    {
      juce::Path tri;
      tri.addTriangle(c.x - 4.f, c.y - 6.f, c.x - 4.f, c.y + 6.f, c.x + 6.f, c.y);
      g.fillPath(tri);
    }
  }

  static void drawJack(juce::Graphics& g, float cx, float cy, float r, bool lit)
  {
    g.setColour(lit ? juce::Colour(0xffff9a6a) : juce::Colour(0xffa9abb0));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(juce::Colour(0xff55575c));
    g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.f);
    g.setColour(juce::Colour(0xff050505));
    g.fillEllipse(cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillEllipse(cx - r * 0.25f, cy - r * 0.25f, r * 0.5f, r * 0.5f);
  }

  static void drawKnob(juce::Graphics& g, float cx, float cy, float r, float bpm, bool sync)
  {
    g.setColour(juce::Colour(0xff030303));
    g.fillEllipse(cx - r - 2.f, cy - r - 2.f, (r + 2.f) * 2.f, (r + 2.f) * 2.f);
    g.setColour(juce::Colour(0xff17181a));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(juce::Colour(0xff4a4c50));
    g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.f);
    const float t = juce::jlimit(0.f, 1.f, (bpm - 30.f) / 270.f);
    const float ang = juce::MathConstants<float>::pi * (-0.75f + 1.5f * t);                 // -135 .. +135 degrees from straight up
    g.setColour(sync ? juce::Colour(0xff777777) : juce::Colour(0xfff2f2f2));
    g.drawLine(cx + std::sin(ang) * r * 0.25f, cy - std::cos(ang) * r * 0.25f,
               cx + std::sin(ang) * r * 0.9f,  cy - std::cos(ang) * r * 0.9f, 2.f);
  }

  static juce::String noteName(int pitch)
  {
    return juce::String(kr106::kNoteNames[((pitch % 12) + 12) % 12]) + juce::String(pitch / 12 - 1);
  }

  // left strip: CLK OUT LED, sync light, and the note / value meter with the scale keys
  void drawLeftStrip(juce::Graphics& g, int pos)
  {
    using namespace kr106;
    g.setColour(juce::Colour(0xffdadada));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    drawLed(g, 76.f, 20.f, 3.6f, mSeq.mSync.load() ? 1.f : 0.f);                       // CLK IN: host sync on
    g.drawText("CLK OUT", 4, 124, 60, 11, juce::Justification::centred);
    drawJack(g, 34.f, 108.f, 11.f, false);
    drawLed(g, 76.f, 108.f, 3.6f, mSeq.mClkOut.load() ? 1.f : 0.f);

    // what the meter shows: the last touched slider for a moment, then the step that is playing
    int kind = 0, val = 0;
    if (juce::Time::getMillisecondCounter() < mBarUntil) { kind = mBarKind; val = mBarVal; }
    else if (!mTrackMode && pos >= 0 && mPage != kVgPgGate)
    {
      const int v = mSeq.mPat.t[mSel].pageArray(mPage)[pos].get();
      if (mPage == kVgPgNote) { kind = 1; val = vgNoteFor(mSeq.mPat, v) % 12; }
      else val = juce::roundToInt(v / 127.f * 12.f);
    }
    const int key = mSeq.mPat.key.get() % 12;
    const uint16_t scale = mSeq.mPat.scale();
    static const bool natural[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    for (int pc = 0; pc < 12; pc++)                                                 // C at the top, like the panel
    {
      const float y = static_cast<float>(kBarY0 + pc * kBarP);
      const bool lit = (kind == 1) ? (pc == val) : ((11 - pc) < val);
      g.setColour(lit ? juce::Colour(0xffffa860) : juce::Colour(0xff5a5c60));
      g.fillRect(static_cast<float>(kBarX), y, static_cast<float>(kBarW), static_cast<float>(kBarH));
      if (lit)
      {
        g.setColour(juce::Colour(0xffff8a30).withAlpha(0.25f));
        g.fillRect(static_cast<float>(kBarX) - 2.f, y - 1.5f, kBarW + 4.f, kBarH + 3.f);
      }
      g.setColour(juce::Colour(0xffdadada));
      g.setFont(juce::FontOptions(9.f, juce::Font::bold));
      if (natural[pc]) g.drawText(kNoteNames[pc], 4, static_cast<int>(y) - 2, 20, 10, juce::Justification::centredLeft);
      else g.fillRect(14.f, y + 2.f, 2.f, 2.f);

      const bool inScale = ((scale >> ((pc - key + 12) % 12)) & 1) != 0;
      const juce::Rectangle<float> kr(static_cast<float>(kKeyX), y, static_cast<float>(kKeyW), static_cast<float>(kBarH));
      g.setColour(natural[pc] ? juce::Colour(0xffe2e2e2) : juce::Colour(0xff1d1d1d));
      g.fillRect(kr);
      if (inScale)
      {
        g.setColour(juce::Colour(0xffff3a1c).withAlpha(natural[pc] ? 0.55f : 0.85f));
        g.fillRect(kr.reduced(2.f, 1.f));
      }
      g.setColour(juce::Colour(0xff606266));
      g.drawRect(kr, 1.f);
    }
  }

  // ---- values shown on the sliders ----
  // handle position (0..1), LED level (0..1) and a short value text for one slider
  void stepInfo(int i, float& frac, float& level, juce::String& text) const
  {
    using namespace kr106;
    const VgTrack& t = mSeq.mPat.t[mSel];
    if (mTrackMode)
    {
      if (i == 0)      { const int d = std::min<int>(kVgNumDiv - 1, t.div.get()); frac = d / 12.f; text = kVgDivNames[d]; }
      else if (i == 1) { const int l = juce::jlimit(1, 16, static_cast<int>(t.len.get())); frac = (l - 1) / 15.f; text = juce::String(l); }
      else if (i == 2) { const int d = std::min<int>(3, t.dir.get()); frac = d / 3.f; text = kVgDirNames[d]; }
      else if (i == 3) { const int k = mSeq.mPat.key.get() % 12; frac = k / 11.f; text = kNoteNames[k]; }
      else
      {
        const bool on = ((mSeq.mPat.scale() >> (i - 4)) & 1) != 0;
        frac = on ? 1.f : 0.f; level = on ? 0.95f : 0.12f;
        text = on ? "ON" : "OFF";
        return;
      }
      level = 0.6f;
      return;
    }
    const int v = const_cast<VgTrack&>(t).pageArray(mPage)[i].get();
    frac = v / 127.f;
    level = 0.15f + 0.55f * frac;
    switch (mPage)
    {
      case kVgPgGate:  frac = v >= 64 ? 1.f : 0.f; level = v >= 64 ? 0.6f : 0.06f; text = v >= 64 ? "ON" : "OFF"; break;
      case kVgPgProb:  text = juce::String(juce::roundToInt(frac * 100.f)) + "%"; break;
      case kVgPgRpt:   text = "x" + juce::String(1 + std::min(7, v / 16)); break;
      case kVgPgDly:   text = juce::String(juce::roundToInt(frac * 90.f)) + "%"; break;
      case kVgPgPw:    text = juce::String(juce::roundToInt((0.05f + 0.95f * frac) * 100.f)) + "%"; break;
      case kVgPgNote:  text = noteName(vgNoteFor(mSeq.mPat, v)); break;
      default:         text = v == 0 ? juce::String("OFF") : juce::String(juce::roundToInt(frac * 100.f)) + "%"; break;
    }
  }

  // the white strip under each slider: what the slider is, and its value
  juce::String stripText(int i, const juce::String& value) const
  {
    using namespace kr106;
    if (mTrackMode)
    {
      static const char* labels[16] = { "DIVIDE/MULTIPLY", "SEQ LENGTH", "SEQ MODE", "KEY", "ROOT", "ROOT + 1", "ROOT + 2", "ROOT + 3",
                                        "ROOT + 4", "ROOT + 5", "ROOT + 6", "ROOT + 7", "ROOT + 8", "ROOT + 9", "ROOT + 10", "ROOT + 11" };
      juce::String s = labels[i];
      if (i >= 4) s += juce::String("  (") + kNoteNames[(mSeq.mPat.key.get() + i - 4) % 12] + ")";
      return s + "   " + value;
    }
    static const char* pages[7] = { "", "PROB", "RPT", "DLY", "PW", "NOTE", "GLIDE" };
    juce::String s = "STEP " + juce::String(i + 1);
    if (mPage != kVgPgGate) s += juce::String("   ") + pages[mPage];
    return s + "   " + value;
  }

  juce::String buttonLabel(const Item& it) const
  {
    if (it.id == kTempo) return mSeq.mSync.load() ? juce::String("TEMPO  SYNC") : "TEMPO  " + juce::String(juce::roundToInt(mSeq.mBpm.load()));
    if (it.id == kLink)
    {
      if (mSel < kr106::kVgGates) return "LINK";
      const int l = mSeq.mPat.t[mSel].link.get();
      return l == 0 ? juce::String("LK -") : "LK G" + juce::String(l);
    }
    return it.label;
  }

  juce::String infoLine() const
  {
    using namespace kr106;
    if (mArm == kArmBank)   return "BANK: press a track button (1-10) to choose the bank";
    if (mArm == kArmSave)   return "SAVE: press a track button (1-10) to save the pattern as that preset";
    if (mArm == kArmRecall) return "RECALL: press a track button (1-10) to load that preset";
    if (mSongPrg)           return "S.PRG: press track buttons to add presets to the song (CLEAR empties it)";
    if (mSeq.mRecArm.load())
      return "REC: play keys or press TAP to record into the step that is playing (" + juce::String(mSel < kVgGates ? "GATE " + juce::String(mSel + 1) : "CV " + juce::String(mSel - kVgGates + 1)) + ")";
    if (mAllSteps)          return "ALL: the next slider move sets every step of the track";
    const int bank = mSeq.mBank.load();
    juce::String song;
    for (int k = 0; k < 10; k++) { const int v = mSeq.mSong[bank][k].get(); if (v > 0) song += " " + juce::String(v); }
    return "BANK " + juce::String(bank + 1) + "   PRESET " + juce::String(mSeq.mSlot.load() + 1) + "   SONG:" + (song.isEmpty() ? juce::String(" -") : song)
           + "     " + (mSel < kVgGates ? "GATE " + juce::String(mSel + 1) : "CV " + juce::String(mSel - kVgGates + 1));
  }

  void setBar(int kind, int val) { mBarKind = kind; mBarVal = val; mBarUntil = juce::Time::getMillisecondCounter() + 1500; }

  // ---- editing ----
  void beginSlider(int step, juce::Point<int> p)
  {
    using namespace kr106;
    VgTrack& t = mSeq.mPat.t[mSel];
    if (mTrackMode && step >= 4)
    {
      const bool on = ((mSeq.mPat.scale() >> (step - 4)) & 1) != 0;
      mSetTo = on ? 0 : 1;
      setScaleBit(step - 4, mSetTo != 0);
      setBar(1, (mSeq.mPat.key.get() + step - 4) % 12);
      return;
    }
    if (!mTrackMode && mPage == kVgPgGate)
    {
      mSetTo = t.gate[step].get() >= 64 ? 0 : 127;
      t.gate[step].set(mSetTo);
      if (mAllSteps) { for (int i = 0; i < kVgSteps; i++) t.gate[i].set(mSetTo); mAllSteps = false; }
      return;
    }
    dragSlider(step, p, true);
  }

  void dragSlider(int step, juce::Point<int> p, bool newStep)
  {
    using namespace kr106;
    VgTrack& t = mSeq.mPat.t[mSel];
    const float f = fracAt(step, p);
    if (mTrackMode)
    {
      if (step >= 4) { if (newStep) { setScaleBit(step - 4, mSetTo != 0); setBar(1, (mSeq.mPat.key.get() + step - 4) % 12); } return; }
      if (step == 0)      { const int d = juce::roundToInt(f * 12.f); t.div.set(d); setBar(0, d); }
      else if (step == 1) { const int l = 1 + juce::roundToInt(f * 15.f); t.len.set(l); setBar(0, juce::roundToInt(l * 12.f / 16.f)); }
      else if (step == 2) { const int d = juce::roundToInt(f * 3.f); t.dir.set(d); setBar(0, (d + 1) * 3); }
      else                { const int k = juce::roundToInt(f * 11.f); mSeq.SetBankKey(k); setBar(1, k); }
      return;
    }
    if (mPage == kVgPgGate) { if (newStep) t.gate[step].set(mSetTo); return; }       // painting on / off
    const int v = juce::roundToInt(f * 127.f);
    t.pageArray(mPage)[step].set(v);
    if (mAllSteps) for (int i = 0; i < kVgSteps; i++) t.pageArray(mPage)[i].set(v);       // ALL: every step gets the value
    if (mPage == kVgPgNote) setBar(1, vgNoteFor(mSeq.mPat, v) % 12);
    else setBar(0, juce::roundToInt(v / 127.f * 12.f));
  }

  void setScaleBit(int bit, bool on)
  {
    uint16_t m = mSeq.mPat.scale();
    if (on) m = static_cast<uint16_t>(m | (1u << bit)); else m = static_cast<uint16_t>(m & ~(1u << bit));
    if ((m & 0x0FFF) == 0) m = static_cast<uint16_t>(1u << bit);          // never an empty scale
    mSeq.SetBankScaleMask(m);                                              // the scale belongs to the bank
  }

  bool isLit(int id, juce::uint32 now) const
  {
    using namespace kr106;
    if (mPressed == id && id != kTempo) return true;
    if (id >= kChanBase && id < kChanBase + 10)
    {
      const int t = id - kChanBase;
      if (mArm == kArmBank) return mSeq.mBank.load() == t;
      if (mArm == kArmSave || mArm == kArmRecall) return mSeq.mSlot.load() == t;
      if (mSongPrg) { for (int k = 0; k < 10; k++) if (mSeq.mSong[mSeq.mBank.load()][k].get() == t + 1) return true; return false; }
      return mSel == t;
    }
    if (id == kPageGate) return !mTrackMode && mPage == kVgPgGate;
    if (id > kPageGate && id <= kPageGlide) return !mTrackMode && mPage == id;
    switch (id)
    {
      case kTrack: return mTrackMode;       case kMute: return mMuteMode;        case kClear: return mClearArm;
      case kSong: return mSeq.mSongOn.load();   case kSongPrg: return mSongPrg;
      case kBank: return mArm == kArmBank;  case kSave: return mArm == kArmSave; case kRecall: return mArm == kArmRecall;
      case kRun: return mSeq.mRun.load();   case kClkIn: return mSeq.mSync.load(); case kFreeze: return mSeq.mFreeze.load();
      case kRnd: return now < mRndFlash;
      case kAllSteps: return mAllSteps;   case kRec: return mSeq.mRecArm.load();   case kGateDly: return mSeq.mGateDelay.load();
      default: return false;
    }
  }

  void press(int id)
  {
    using namespace kr106;
    if (id >= kChanBase && id < kChanBase + 10)
    {
      const int t = id - kChanBase, bank = mSeq.mBank.load();
      if (mArm == kArmBank)        { mSeq.mBank.store(t); mSeq.ApplyBankScale(t); mArm = kArmNone; }
      else if (mArm == kArmSave)   { mSeq.Save(bank * 10 + t); mSeq.mSlot.store(t); mArm = kArmNone; }
      else if (mArm == kArmRecall) { mSeq.Recall(bank * 10 + t); mArm = kArmNone; }
      else if (mSongPrg)
      {
        for (int k = 0; k < 10; k++) if (mSeq.mSong[bank][k].get() == 0) { mSeq.mSong[bank][k].set(t + 1); break; }
      }
      else if (mClearArm)          { mSeq.ClearTrack(t); mClearArm = false; }
      else if (mMuteMode)          mSeq.mPat.t[t].mute.set(mSeq.mPat.t[t].mute.get() ? 0 : 1);
      else                         { mSel = t; mSeq.mRecTrack.store(t); }
      return;
    }
    if (id >= kPageProb && id <= kPageGlide)                // tapping the lit page button goes back to GATE
    {
      if (!mTrackMode && mPage == id) mPage = kPageGate; else { mPage = id; mTrackMode = false; }
      return;
    }
    switch (id)
    {
      case kPageGate: mPage = kPageGate; mTrackMode = false; break;
      case kTrack: mTrackMode = !mTrackMode; break;
      case kMute:  mMuteMode = !mMuteMode; break;
      case kClear:
        if (mSongPrg) { for (int k = 0; k < 10; k++) mSeq.mSong[mSeq.mBank.load()][k].set(0); }
        else mClearArm = !mClearArm;
        break;
      case kBank:   mArm = (mArm == kArmBank)   ? kArmNone : kArmBank;   break;
      case kSave:   mArm = (mArm == kArmSave)   ? kArmNone : kArmSave;   break;
      case kRecall: mArm = (mArm == kArmRecall) ? kArmNone : kArmRecall; break;
      case kSong:   mSeq.mSongOn.store(!mSeq.mSongOn.load()); break;
      case kSongPrg: mSongPrg = !mSongPrg; break;
      case kLink:
        if (mSel >= kVgGates) mSeq.mPat.t[mSel].link.set((mSeq.mPat.t[mSel].link.get() + 1) % (kVgGates + 1));
        break;
      case kRun:    mSeq.mRun.store(!mSeq.mRun.load()); break;
      case kReset:  mSeq.RequestReset(); break;
      case kClkIn:  mSeq.mSync.store(!mSeq.mSync.load()); break;
      case kFreeze: mSeq.mFreeze.store(!mSeq.mFreeze.load()); break;
      case kRnd:
        if (mTrackMode) mSeq.RandomizeAll(mSel); else mSeq.Randomize(mSel, mPage);
        mRndFlash = juce::Time::getMillisecondCounter() + 200;
        break;
      case kRndAll: mSeq.RandomizeAll(mSel); break;
      case kRotL:   mSeq.Rotate(mSel, -1); break;
      case kRotR:   mSeq.Rotate(mSel, +1); break;
      case kAllSteps: mAllSteps = !mAllSteps; break;
      case kRec:    mSeq.mRecArm.store(!mSeq.mRecArm.load()); mSeq.mRecTrack.store(mSel); break;
      case kTap:    mSeq.RecordEvent(mSel, -1); break;
      case kGateDly: mSeq.mGateDelay.store(!mSeq.mGateDelay.load()); break;
      default: break;
    }
  }
};
