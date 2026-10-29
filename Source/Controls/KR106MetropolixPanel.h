#pragma once

// "Seq" page, Metropolix mode: a step sequencer panel in the style of the Intellijel Metropolix.
//
//   * Right: PITCH sliders (8 stages, black caps with a red rectangular LED), PULSE COUNT sliders (1-8)
//     and GATE TYPE sliders (hold / multi / single / rest), and the row of 8 stage buttons with their
//     function (SLIDE SKIP PITCH GATE RATCH PROB ACCUM CV) and ALT (LOAD SAVE A B CLK MX LIM TYPE) labels.
//   * Left: the screen and the encoder (drag it, tap it to push), EXIT, TRK 1 / TRK 2 / MOD, RUN, the loopy
//     button, RESET, SETUP, BPM, SCALE, ORDER, LEN, DIV, SWING, SLIDE, GATE, ALT and EDIT.
//
//   How it is played (the same as the hardware, adapted to touch):
//     - ORDER / LEN / DIV / SWING / SLIDE / GATE / BPM / SCALE open their screen: the encoder changes the
//       value, a tap on the encoder switches to the next value of that screen (or reverses the order).
//     - EDIT, then a function button (SLIDE ... CV): the 8 stage buttons then choose the stage; the encoder
//       edits that stage's value (a tap on the encoder changes its second setting). SLIDE and SKIP toggle.
//     - ALT, then LOAD or SAVE: the encoder chooses the bank, then a stage button picks the preset.
//     - SETUP is the track menu: transposition, slider range, pulse count / gate type override, gate
//       stretching, ratchet mode, slide type, accumulator mode / limits, mute.
//     - MOD: the sliders edit the 8 MOD lane values of the lane chosen with the encoder; ORDER / LEN / DIV
//       then edit that lane.
//     - The loopy button (infinity): while it is on, holding a stage button loops that stage (hold two for
//       a range). Without loopy or EDIT, a stage button plays that stage (stage player).
//
// All data lives in the DSP (kr106::MetropolixSeq); this panel only edits it and shows its state.

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../PluginProcessor.h"
#include "../DSP/KR106Metropolix.h"
#include "../DSP/KR106Scale.h"

class KR106MetropolixPanel : public juce::Component, private juce::Timer
{
public:
  explicit KR106MetropolixPanel(KR106AudioProcessor& p) : mProc(p), mSeq(p.getMetro())
  {
    startTimerHz(30);
  }

  void resized() override { layout(); }

  void paint(juce::Graphics& g) override
  {
    using namespace kr106;
    g.fillAll(juce::Colour(0xffc9cacd));                                  // the silver panel
    g.setColour(juce::Colour(0xff8d8e92));
    g.drawRect(getLocalBounds(), 1);

    // slider area: dark background like the original
    g.setColour(juce::Colour(0xff1d1e21));
    g.fillRect(kSlX0 - 10, 2, 870 - kSlX0 + 10 - 2, 350);
    drawSliders(g);
    drawStageRow(g);

    drawScreen(g);

    // buttons, jacks, encoder
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    for (auto& it : mItems)
    {
      const auto c = it.r.getCentre().toFloat();
      const bool lit = isLit(it.id, now);
      g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
      g.setColour(juce::Colour(0xff25262a));
      switch (it.shape)
      {
        case Round:
          drawRoundButton(g, c.x, c.y, 13.f, lit);
          g.drawText(it.label, it.r.getX() - 16, it.r.getBottom() + 1, it.r.getWidth() + 32, 11, juce::Justification::centred);
          break;
        case Jack:
          drawJack(g, c.x, c.y, 11.f, lit);
          g.drawText(it.label, it.r.getX() - 16, it.r.getBottom() + 1, it.r.getWidth() + 32, 11, juce::Justification::centred);
          break;
        case Encoder:
          drawEncoder(g, c.x, c.y, 30.f);
          break;
        case Led:
          drawLed(g, c.x, c.y, 4.f, lit ? 1.f : 0.08f);
          g.drawText(it.label, it.r.getX() - 16, it.r.getBottom() + 1, it.r.getWidth() + 32, 11, juce::Justification::centred);
          break;
      }
    }
  }

  void mouseDown(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    mPressed = -1; mSlider = -1; mEncDrag = false;
    int band = -1, col = -1;
    if (sliderAt(p, band, col)) { mSlider = band * 8 + col; setSlider(band, col, p.y); return; }
    for (auto& it : mItems)
    {
      if (!it.r.contains(p)) continue;
      if (it.id == kEncoder) { mEncDrag = true; mEncLast = p; mEncAcc = 0; mEncMoved = 0; return; }
      mPressed = it.id;
      break;
    }
    if (mPressed < 0)
      for (int i = 0; i < 8; i++)
        if (stageRect(i).contains(p)) { mPressed = kStageBase + i; stageDown(i); return; }
    repaint();
  }

  void mouseDrag(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    if (mSlider >= 0) { setSlider(mSlider / 8, mSlider % 8, p.y); return; }
    if (mEncDrag)
    {
      mEncAcc += (mEncLast.y - p.y) + (p.x - mEncLast.x);
      mEncMoved += std::abs(p.y - mEncLast.y) + std::abs(p.x - mEncLast.x);
      mEncLast = p;
      while (mEncAcc >= 7) { encoder(+1); mEncAcc -= 7; }
      while (mEncAcc <= -7) { encoder(-1); mEncAcc += 7; }
    }
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    const auto p = e.getPosition();
    if (mEncDrag) { if (mEncMoved < 5) encoderPress(); mEncDrag = false; }
    else if (mPressed >= kStageBase && mPressed < kStageBase + 8) stageUp(mPressed - kStageBase);
    else if (mPressed >= 0)
    {
      bool inside = false;
      for (auto& it : mItems) if (it.id == mPressed && it.r.contains(p)) inside = true;
      if (inside) press(mPressed);
    }
    mPressed = -1; mSlider = -1;
    repaint();
  }

private:
  // ---- geometry (panel coordinates) ----
  static constexpr int kSlX0 = 448, kColP = 52, kColX0 = 474;
  static constexpr int kPitchY0 = 22, kPitchY1 = 122, kPulseY0 = 158, kPulseY1 = 212, kGateY0 = 242, kGateY1 = 282;
  static constexpr int kStageCy = 336, kStageR = 12;
  enum Id {
    kTrk1 = 1, kTrk2, kMod, kRun, kLoopy, kReset, kSetup, kBpm, kScale, kOrder, kLen, kDiv, kSwing, kSlide, kGateLen,
    kAlt, kEdit, kExit, kClk, kRstJack, kJackA, kJackB, kLed1, kLed2, kEncoder, kStageBase = 100
  };
  enum Shape { Round, Jack, Encoder, Led };
  enum Screen { kHome, kScOrder, kScLen, kScDiv, kScSwing, kScSlide, kScGate, kScBpm, kScScale, kScMenu, kScEdit, kScLoad, kScSave, kScLim, kScInfo };
  struct Item { int id; Shape shape; juce::Rectangle<int> r; juce::String label; };

  KR106AudioProcessor& mProc;
  kr106::MetropolixSeq& mSeq;
  std::vector<Item> mItems;
  int mTrk = 0, mLane = 0, mScreen = kHome, mSub = 0, mEditLane = 0, mEditStage = 0, mBank = 0, mMenuItem = 0, mInfo = 0;
  bool mMod = false, mEdit = false, mEditPick = false, mAlt = false, mLoopy = false, mMenuEditing = false;
  int mPressed = -1, mSlider = -1, mHeldMask = 0, mLoopFirst = -1;
  bool mEncDrag = false; juce::Point<int> mEncLast; int mEncAcc = 0, mEncMoved = 0;
  juce::uint32 mFlashUntil = 0;

  void timerCallback() override { if (isShowing()) repaint(); }

  static constexpr int sx(int col) { return kColX0 + col * kColP; }

  void addItem(int id, Shape shape, int cx, int cy, int size, const juce::String& label)
  {
    mItems.push_back({ id, shape, juce::Rectangle<int>(size, size).withCentre({ cx, cy }), label });
  }

  void layout()
  {
    mItems.clear();
    addItem(kJackA, Jack, 24, 22, 26, "A");   addItem(kJackB, Jack, 62, 22, 26, "B");
    addItem(kClk, Jack, 100, 22, 26, "CLK");  addItem(kRstJack, Jack, 138, 22, 26, "RESET");
    addItem(kLed1, Led, 196, 22, 14, "GATE 1"); addItem(kLed2, Led, 240, 22, 14, "GATE 2");
    addItem(kEncoder, Encoder, 392, 92, 64, "");
    addItem(kExit, Round, 424, 140, 24, "EXIT");
    addItem(kTrk1, Round, 150, 176, 28, "TRK 1"); addItem(kTrk2, Round, 205, 176, 28, "TRK 2"); addItem(kMod, Round, 260, 176, 28, "MOD");
    const int bx[6] = { 30, 88, 146, 204, 262, 320 };
    const int ids1[6] = { kRun, kLoopy, kReset, kSetup, kBpm, kScale };
    const char* l1[6] = { "RUN", "LOOPY", "RESET", "SETUP", "BPM", "SCALE" };
    const int ids2[6] = { kOrder, kLen, kDiv, kSwing, kSlide, kGateLen };
    const char* l2[6] = { "ORDER", "LEN", "DIV", "SWING", "SLIDE", "GATE" };
    for (int i = 0; i < 6; i++) { addItem(ids1[i], Round, bx[i], 224, 28, l1[i]); addItem(ids2[i], Round, bx[i], 280, 28, l2[i]); }
    addItem(kAlt, Round, 398, 296, 26, "ALT");
    addItem(kEdit, Round, 398, 336, 26, "EDIT");
  }

  juce::Rectangle<int> stageRect(int i) const { return juce::Rectangle<int>(30, 30).withCentre({ sx(i), kStageCy }); }

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
    g.setColour(juce::Colour(0xff000000).withAlpha(0.7f));
    g.drawRoundedRectangle(r, 2.f, 1.f);
  }
  // light grey caps; lit = a white glow
  static void drawRoundButton(juce::Graphics& g, float cx, float cy, float r, bool lit)
  {
    if (lit)
    {
      g.setColour(juce::Colour(0xff7fb2ff).withAlpha(0.25f));
      g.fillEllipse(cx - r * 1.8f, cy - r * 1.8f, r * 3.6f, r * 3.6f);
    }
    g.setColour(juce::Colour(0xff6c6e74));
    g.fillEllipse(cx - r - 1.5f, cy - r - 1.5f, (r + 1.5f) * 2.f, (r + 1.5f) * 2.f);
    g.setColour(lit ? juce::Colour(0xfff6f9ff) : juce::Colour(0xffe0e1e4));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(juce::Colour(0xffffffff).withAlpha(lit ? 0.9f : 0.55f));
    g.fillEllipse(cx - r * 0.65f, cy - r * 0.8f, r * 1.3f, r * 1.05f);
  }
  static void drawJack(juce::Graphics& g, float cx, float cy, float r, bool lit)
  {
    g.setColour(lit ? juce::Colour(0xffff9a6a) : juce::Colour(0xff2a2b2f));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(juce::Colour(0xff8c8e93));
    g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 2.f);
    g.setColour(juce::Colour(0xff050505));
    g.fillEllipse(cx - r * 0.5f, cy - r * 0.5f, r, r);
  }
  void drawEncoder(juce::Graphics& g, float cx, float cy, float r) const
  {
    g.setColour(juce::Colour(0xff111214));
    g.fillEllipse(cx - r - 3.f, cy - r - 3.f, (r + 3.f) * 2.f, (r + 3.f) * 2.f);
    g.setColour(juce::Colour(0xff2c2d31));
    g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
    g.setColour(juce::Colour(0xff55565b));
    g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.5f);
    const float ang = juce::MathConstants<float>::pi * 0.02f * static_cast<float>(mEncTurn);      // the knob turns with the encoder
    for (int k = 0; k < 12; k++)
    {
      const float a = ang + k * juce::MathConstants<float>::pi / 6.f;
      g.setColour(juce::Colour(0xff4a4b50));
      g.drawLine(cx + std::sin(a) * r * 0.86f, cy - std::cos(a) * r * 0.86f, cx + std::sin(a) * r * 0.97f, cy - std::cos(a) * r * 0.97f, 1.5f);
    }
    g.setColour(juce::Colour(0xffe8e8ea));
    g.drawLine(cx + std::sin(ang) * r * 0.3f, cy - std::cos(ang) * r * 0.3f, cx + std::sin(ang) * r * 0.78f, cy - std::cos(ang) * r * 0.78f, 3.f);
  }
  int mEncTurn = 0;

  static juce::String noteName(int n) { return juce::String(kr106::kNoteNames[((n % 12) + 12) % 12]) + juce::String(n / 12); }

  // sliders: vertical caps with a big rectangular LED
  void drawCap(juce::Graphics& g, float cx, float cy, float w, float h, float level) const
  {
    const juce::Rectangle<float> cap(cx - w * 0.5f, cy - h * 0.5f, w, h);
    g.setColour(juce::Colour(0xff060606));
    g.fillRoundedRectangle(cap, 3.f);
    g.setColour(juce::Colour(0xff5a5b60));
    g.drawRoundedRectangle(cap.reduced(0.5f), 3.f, 1.f);
    if (level >= 0.f) drawRectLed(g, juce::Rectangle<float>(0.f, 0.f, w - 8.f, h - 8.f > 8.f ? 9.f : h - 8.f).withCentre(cap.getCentre()), level);
  }

  static float yFor(float frac, int y0, int y1) { return static_cast<float>(y1) - frac * static_cast<float>(y1 - y0); }   // frac 1 = top (y0)

  void drawSliders(juce::Graphics& g)
  {
    using namespace kr106;
    const MxPattern& P = mSeq.mPat;
    const int posA = mSeq.mPos[mTrk].load(std::memory_order_relaxed), posB = mSeq.mPos[1 - mTrk].load(std::memory_order_relaxed);
    g.setFont(juce::FontOptions(10.f, juce::Font::bold));
    g.setColour(juce::Colour(0xffe8e8ea));
    g.drawText(mMod ? "MOD LANE " + juce::String(mLane + 1) : juce::String("PITCH"), kSlX0, 3, 870 - kSlX0, 13, juce::Justification::centred);
    g.drawText(mMod ? "RAMP" : "PULSE COUNT", kSlX0, 138, 870 - kSlX0, 13, juce::Justification::centred);
    g.drawText(mMod ? "GATE" : "GATE TYPE", kSlX0, 224, 870 - kSlX0, 13, juce::Justification::centred);
    g.setColour(juce::Colour(0xff6a6b70));
    g.fillRect(kSlX0, 16, 870 - kSlX0 - 6, 1); g.fillRect(kSlX0, 151, 870 - kSlX0 - 6, 1); g.fillRect(kSlX0, 237, 870 - kSlX0 - 6, 1);

    for (int i = 0; i < kMxStages; i++)
    {
      const int cx = sx(i);
      // slot lines
      g.setColour(juce::Colour(0xff0a0a0a));
      g.fillRoundedRectangle(static_cast<float>(cx - 4), static_cast<float>(kPitchY0 - 6), 8.f, static_cast<float>(kPitchY1 - kPitchY0 + 12), 3.f);
      g.fillRoundedRectangle(static_cast<float>(cx - 4), static_cast<float>(kPulseY0 - 6), 8.f, static_cast<float>(kPulseY1 - kPulseY0 + 12), 3.f);
      g.fillRoundedRectangle(static_cast<float>(cx - 4), static_cast<float>(kGateY0 - 6), 8.f, static_cast<float>(kGateY1 - kGateY0 + 12), 3.f);

      const bool playing = (i == posA), other = (i == posB);
      const float lvl = playing ? 1.f : (other ? 0.45f : 0.1f);

      // pitch (or MOD lane value)
      float f;
      if (mMod) f = (s8(P.lane[mLane].val[i]) + 128) / 255.f; else f = P.pitch[i].get() / 127.f;
      drawCap(g, static_cast<float>(cx), yFor(f, kPitchY0, kPitchY1), 30.f, 18.f, mMod ? 0.2f + 0.7f * f : lvl);
      g.setColour(juce::Colour(0xffd8d8da));
      g.setFont(juce::FontOptions(10.f, juce::Font::bold));
      g.drawText(juce::String(i + 1), cx - 10, kPitchY1 + 12, 20, 11, juce::Justification::centred);

      // pulse count 1..8 (numbers 8..1 beside the slots) / lane ramp
      const float fp = mMod ? (P.lane[mLane].ramp[i].get() ? 1.f : 0.f) : (juce::jlimit(1, 8, static_cast<int>(P.pulse[i].get())) - 1) / 7.f;
      g.setFont(juce::FontOptions(7.5f, juce::Font::bold));
      g.setColour(juce::Colour(0xffb4b5ba));
      if (!mMod) for (int n = 1; n <= 8; n++) g.drawText(juce::String(n), cx - 21, juce::roundToInt(yFor((n - 1) / 7.f, kPulseY0, kPulseY1)) - 4, 10, 8, juce::Justification::centredRight);
      drawCap(g, static_cast<float>(cx), yFor(fp, kPulseY0, kPulseY1), 26.f, 13.f, playing && !mMod ? 0.9f : -1.f);

      // gate type (hold / multi / single / rest) / lane gate
      const int gt = std::min<int>(3, P.gtype[i].get());
      const float fg = mMod ? (P.lane[mLane].gate[i].get() ? 1.f : 0.f) : 1.f - gt / 3.f;
      if (!mMod) for (int n = 0; n < 4; n++) drawGateIcon(g, cx - 18, juce::roundToInt(yFor(1.f - n / 3.f, kGateY0, kGateY1)), n);
      drawCap(g, static_cast<float>(cx), yFor(fg, kGateY0, kGateY1), 24.f, 12.f, -1.f);
    }
  }

  // the little symbols beside the gate type sliders
  static void drawGateIcon(juce::Graphics& g, int x, int y, int type)
  {
    g.setColour(juce::Colour(0xffd0d1d4));
    switch (type)
    {
      case 0: g.fillRect(x, y - 1, 10, 2); break;                                   // hold: one long bar
      case 1: g.fillRect(x, y - 1, 3, 2); g.fillRect(x + 4, y - 1, 3, 2); g.fillRect(x + 8, y - 1, 3, 2); break;   // multi: several short ones
      case 2: g.fillRect(x, y - 1, 4, 2); break;                                    // single: one short
      default: g.drawRect(x, y - 2, 6, 4, 1); break;                                // rest: an empty box
    }
  }

  void drawStageRow(juce::Graphics& g)
  {
    using namespace kr106;
    static const char* fn[8] = { "SLIDE", "SKIP", "PITCH", "GATE", "RATCH", "PROB", "ACCUM", "CV" };
    static const char* alt[8] = { "LOAD", "SAVE", "A", "B", "CLK", "MX", "LIM", "TYPE" };
    const int pos = mSeq.mPos[mTrk].load(std::memory_order_relaxed);
    const bool stageMode = mEdit && !mEditPick;
    for (int i = 0; i < 8; i++)
    {
      const int cx = sx(i);
      g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
      g.setColour(mAlt ? juce::Colour(0xff8ab4ff) : juce::Colour(0xff6c6d72));
      g.drawText(alt[i], cx - 26, 301, 52, 10, juce::Justification::centred);
      g.setColour((mEdit && mEditPick) ? juce::Colour(0xfff2f2f4) : juce::Colour(0xffb4b5ba));
      g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
      g.drawText(fn[i], cx - 26, 312, 52, 11, juce::Justification::centred);

      bool lit = false;
      if (mAlt) lit = false;
      else if (stageMode) lit = (i == mEditStage) || stageFlag(i);
      else if (mEdit) lit = (i == mEditLane);
      else lit = (i == pos);
      if (mPressed == kStageBase + i) lit = true;
      drawRoundButton(g, static_cast<float>(cx), static_cast<float>(kStageCy), static_cast<float>(kStageR), lit);
      g.setColour(juce::Colour(0xff3a3b40));
      g.setFont(juce::FontOptions(9.f, juce::Font::bold));
      g.drawText(juce::String(i + 1), cx - 8, kStageCy - 6, 16, 12, juce::Justification::centred);
    }
  }

  // does the stage have something set in the lane that is being edited?
  bool stageFlag(int i) const
  {
    const kr106::MxStage& S = mSeq.mPat.trk[mTrk].st[i];
    switch (mEditLane)
    {
      case 0: return S.slide.get() != 0;
      case 1: return S.skip.get() != 0;
      case 2: return S.pitchOvr.get() != 0;
      case 3: return S.gateOvr.get() != 0;
      case 4: return S.ratch.get() > 1;
      case 5: return S.prob.get() < 100;
      case 6: return kr106::s8(S.accAmt) != 0;
      default: return kr106::s8(S.cvVal) != 0;
    }
  }

  // ---- the screen ----
  static juce::String onOff(bool b) { return b ? "On" : "Off"; }

  std::vector<juce::String> screenLines() const
  {
    using namespace kr106;
    const MxTrack& T = mSeq.mPat.trk[mTrk];
    const MxLane& L = mSeq.mPat.lane[mLane];
    auto mark = [&](int sub) { return juce::String(mSub == sub ? "> " : "  "); };
    std::vector<juce::String> v;
    const juce::String who = mMod ? "MOD " + juce::String(mLane + 1) : "TRK " + juce::String(mTrk + 1);
    switch (mScreen)
    {
      case kScOrder:
        v.push_back(who + "  ORDER");
        v.push_back(juce::String(kMxOrderNames[std::min<int>(kMxOrders - 1, mMod ? L.order.get() : T.order.get())]));
        v.push_back((mMod ? L.dir.get() : T.dir.get()) ? "Reverse  (tap: forward)" : "Forward  (tap: reverse)");
        break;
      case kScLen:
        v.push_back(who + "  LENGTH");
        v.push_back(mark(0) + "Stages  " + juce::String(mMod ? L.stages.get() : T.stages.get()));
        v.push_back(mark(1) + "Offset  " + juce::String(mMod ? L.offset.get() : T.offset.get()));
        if (!mMod) v.push_back(mark(2) + "Pulses  " + (T.pulses.get() ? juce::String(T.pulses.get()) : juce::String("Auto")));
        break;
      case kScDiv:
        v.push_back(who + "  CLOCK");
        v.push_back(mark(0) + "Clock div  /" + juce::String(mMod ? L.clkDiv.get() : T.clkDiv.get()));
        if (!mMod) v.push_back(mark(1) + "Pulse div  /" + juce::String(T.pulseDiv.get()));
        break;
      case kScSwing: v.push_back(who + "  SWING"); v.push_back(juce::String(T.swing.get()) + " %"); break;
      case kScSlide:
        v.push_back(who + "  SLIDE");
        v.push_back(mark(0) + "Amount  " + juce::String(T.slideAmt.get()) + " %");
        v.push_back(mark(1) + "Type  " + kMxSlideTypes[std::min<int>(2, T.slideType.get())]);
        break;
      case kScGate:
        v.push_back(who + "  GATE");
        v.push_back(mark(0) + "Length  " + (T.gateLen.get() >= 100 ? juce::String("Hold") : juce::String(T.gateLen.get()) + " %"));
        v.push_back(mark(1) + "Scale  " + juce::String(T.gateScale.get()) + " %");
        v.push_back(mark(2) + "Stretch  " + onOff(T.stretch.get() != 0));
        break;
      case kScBpm:
        v.push_back("TEMPO"); v.push_back(mSeq.mSync.load() ? juce::String("Host sync") : juce::String(juce::roundToInt(mSeq.mBpm.load())) + " BPM");
        v.push_back(juce::String("tap: ") + (mSeq.mSync.load() ? "internal clock" : "sync to host"));
        break;
      case kScScale:
      {
        v.push_back("SCALE");
        v.push_back(mark(0) + kScaleDefs[std::min<int>(kNumScaleTypes - 1, mSeq.mPat.scaleType.get())].name);
        v.push_back(mark(1) + "Root  " + kNoteNames[mSeq.mPat.root.get() % 12]);
        break;
      }
      case kScMenu:
      {
        v.push_back(who + "  TRACK MENU" + juce::String(mMenuEditing ? "  [edit]" : ""));
        for (int k = -1; k <= 2; k++)
        {
          const int it = mMenuItem + k;
          if (it < 0 || it >= kMenuItems) { v.push_back(""); continue; }
          v.push_back(juce::String(k == 0 ? (mMenuEditing ? "* " : "> ") : "  ") + menuText(it));
        }
        break;
      }
      case kScEdit:
      {
        static const char* names[8] = { "SLIDE", "SKIP", "PITCH", "GATE", "RATCHET", "PROBABILITY", "ACCUMULATOR", "CV" };
        v.push_back(who + "  " + names[mEditLane]);
        v.push_back("Stage " + juce::String(mEditStage + 1) + "   " + editValueText(0));
        const juce::String second = editValueText(1);
        if (second.isNotEmpty()) v.push_back(second);
        break;
      }
      case kScLoad: case kScSave:
        v.push_back(mScreen == kScLoad ? "LOAD" : "SAVE");
        v.push_back("Bank " + juce::String(mBank + 1));
        v.push_back(juce::String("press a stage button (1-8)"));
        break;
      case kScLim:
        v.push_back(who + "  ACCUM LIMITS");
        v.push_back(mark(0) + "Positive  " + juce::String(T.limPos.get()));
        v.push_back(mark(1) + "Negative  " + juce::String(T.limNeg.get()));
        break;
      case kScInfo:
        v.push_back("ALT");
        v.push_back(mInfo == 0 ? "Output A: not used here" : mInfo == 1 ? "Output B: not used here" : mInfo == 2 ? "Clock out: not used here" : "Not used in the plugin");
        v.push_back("(use the modulation matrix)");
        break;
      default:
      {
        v.push_back(who + (mMod ? juce::String("  ") + juce::String(L.type.get() == 2 ? "gate" : L.type.get() == 1 ? "unipolar" : "bipolar")
                                : juce::String("  ") + kMxOrderNames[std::min<int>(kMxOrders - 1, T.order.get())] + (T.dir.get() ? " <" : " >")));
        if (mMod) v.push_back(juce::String(kMxOrderNames[std::min<int>(kMxOrders - 1, L.order.get())]) + "  /" + juce::String(L.clkDiv.get()));
        else v.push_back("Stages " + juce::String(T.stages.get()) + "  Off " + juce::String(T.offset.get()) + "  /" + juce::String(T.clkDiv.get()));
        break;
      }
    }
    return v;
  }

  static constexpr int kMenuItems = 15;
  juce::String menuText(int it) const
  {
    using namespace kr106;
    const MxTrack& T = mSeq.mPat.trk[mTrk];
    switch (it)
    {
      case 0:  return "Transpose  " + juce::String(s8(T.transpose));
      case 1:  return "Slider octaves  " + juce::String(T.sliderOct.get());
      case 2:  return juce::String("Slider dir  ") + (T.sliderDir.get() ? "Down" : "Up");
      case 3:  return "Pulse count  " + (T.pulseOvr.get() ? juce::String(T.pulseOvr.get()) : juce::String("Switch"));
      case 4:  return "Gate type  " + juce::String(kMxGateTypeOvr[std::min<int>(6, T.gateTypeOvr.get())]);
      case 5:  return "Stretching  " + onOff(T.stretch.get() != 0);
      case 6:  return "Ratchet mode  " + juce::String(kMxRatchModes[std::min<int>(2, T.ratchMode.get())]);
      case 7:  return "Slide type  " + juce::String(kMxSlideTypes[std::min<int>(2, T.slideType.get())]);
      case 8:  return juce::String("Accum mode  ") + (T.accMode.get() ? "Track" : "Stage");
      case 9:  return juce::String("Accum order  ") + (T.accOrder.get() ? "Pendulum" : "Wrap");
      case 10: return juce::String("Accum polarity  ") + (T.accPolar.get() ? "Bipolar" : "Unipolar");
      case 11: return "Accum limit +  " + juce::String(T.limPos.get());
      case 12: return "Accum limit -  " + juce::String(T.limNeg.get());
      case 13: return "Mute  " + onOff(T.mute.get() != 0);
      default: return "Rest pitch  " + juce::String(T.restPitch.get() ? "Update" : "Hold");
    }
  }

  juce::String editValueText(int which) const
  {
    using namespace kr106;
    const MxStage& S = mSeq.mPat.trk[mTrk].st[mEditStage];
    switch (mEditLane)
    {
      case 0: return which == 0 ? onOff(S.slide.get() != 0) : juce::String();
      case 1: return which == 0 ? onOff(S.skip.get() != 0) : juce::String();
      case 2: return which == 0 ? (S.pitchOvr.get() ? noteName(S.pitchOvr.get() - 1) : juce::String("--")) : juce::String();
      case 3: return which == 0 ? (S.gateOvr.get() == 0 ? juce::String("--") : S.gateOvr.get() == 1 ? juce::String("Rest")
                                    : (S.gateOvr.get() >= 101 ? juce::String("Hold") : juce::String(S.gateOvr.get() - 1) + " %")) : juce::String();
      case 4: return which == 0 ? "x" + juce::String(S.ratch.get()) : juce::String();
      case 5: return which == 0 ? juce::String(S.prob.get()) + " %" : juce::String("target: ") + (S.probTarget.get() ? "pulse" : "stage") + "  (tap)";
      case 6: return which == 0 ? juce::String(s8(S.accAmt)) : juce::String("trigger: ") + (S.accTrig.get() == 0 ? "stage" : S.accTrig.get() == 1 ? "pulse" : "ratchet") + "  (tap)";
      default: return which == 0 ? juce::String(s8(S.cvVal)) : juce::String("ramp: ") + (S.cvRamp.get() == 0 ? "step" : S.cvRamp.get() == 1 ? "stage" : "pulse") + "  (tap)";
    }
  }

  void drawScreen(juce::Graphics& g)
  {
    using namespace kr106;
    const juce::Rectangle<int> r(130, 44, 206, 100);
    g.setColour(juce::Colour(0xff141516));
    g.fillRoundedRectangle(r.expanded(4).toFloat(), 4.f);
    g.setColour(juce::Colour(0xff000000));
    g.fillRect(r);
    g.setColour(juce::Colour(0xffececf0));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    const auto lines = screenLines();
    int y = r.getY() + 3;
    for (auto& l : lines) { g.drawText(l, r.getX() + 6, y, r.getWidth() - 8, 14, juce::Justification::centredLeft); y += 15; }

    // the stages as small squares: the playing one is filled, skipped ones are crossed
    const int pos = mSeq.mPos[mTrk].load(std::memory_order_relaxed);
    for (int i = 0; i < 8; i++)
    {
      const juce::Rectangle<float> q(static_cast<float>(r.getX() + 8 + i * 24), static_cast<float>(r.getBottom() - 18), 16.f, 12.f);
      const bool skipped = !mMod && mSeq.mPat.trk[mTrk].st[i].skip.get();
      g.setColour(juce::Colour(0xffececf0));
      if (!mMod && i == pos) g.fillRect(q); else g.drawRect(q, 1.f);
      if (skipped) { g.drawLine(q.getX(), q.getY(), q.getRight(), q.getBottom(), 1.f); g.drawLine(q.getX(), q.getBottom(), q.getRight(), q.getY(), 1.f); }
      if (mEdit && !mEditPick && i == mEditStage) g.drawRect(q.expanded(2.f), 1.f);
    }
  }

  // ---- editing ----
  static void bump(kr106::A8& a, int d, int lo, int hi) { a.set(juce::jlimit(lo, hi, static_cast<int>(a.get()) + d)); }
  static void bumpS(kr106::A8& a, int d, int lo, int hi) { kr106::s8set(a, juce::jlimit(lo, hi, kr106::s8(a) + d)); }

  void encoder(int d)
  {
    using namespace kr106;
    mEncTurn += d;
    MxTrack& T = mSeq.mPat.trk[mTrk];
    MxLane& L = mSeq.mPat.lane[mLane];
    switch (mScreen)
    {
      case kHome:
        if (mMod) mLane = juce::jlimit(0, 7, mLane + d); else mTrk = juce::jlimit(0, 1, mTrk + d);
        break;
      case kScOrder: if (mMod) bump(L.order, d, 0, kMxOrders - 1); else bump(T.order, d, 0, kMxOrders - 1); break;
      case kScLen:
        if (mMod) { if (mSub == 0) bump(L.stages, d, 1, 8); else bump(L.offset, d, 0, 7); break; }
        if (mSub == 0) bump(T.stages, d, 1, 8); else if (mSub == 1) bump(T.offset, d, 0, 7); else bump(T.pulses, d, 0, 64);
        break;
      case kScDiv:
        if (mMod) { bump(L.clkDiv, d, 1, 64); break; }
        if (mSub == 0) bump(T.clkDiv, d, 1, 64); else bump(T.pulseDiv, d, 1, 8);
        break;
      case kScSwing: bump(T.swing, d, 50, 78); break;
      case kScSlide: if (mSub == 0) bump(T.slideAmt, d, 0, 100); else bump(T.slideType, d, 0, 2); break;
      case kScGate: if (mSub == 0) bump(T.gateLen, d, 1, 100); else if (mSub == 1) bump(T.gateScale, d, 1, 100); else T.stretch.set(d > 0 ? 1 : 0); break;
      case kScBpm: mSeq.mBpm.store(juce::jlimit(30.f, 300.f, mSeq.mBpm.load() + static_cast<float>(d))); break;
      case kScScale:
        if (mSub == 0) bump(mSeq.mPat.scaleType, d, 0, kNumScaleTypes - 1);
        else mSeq.mPat.root.set(((mSeq.mPat.root.get() + d) % 12 + 12) % 12);
        break;
      case kScMenu:
        if (mMenuEditing) menuEdit(mMenuItem, d); else mMenuItem = juce::jlimit(0, kMenuItems - 1, mMenuItem + d);
        break;
      case kScEdit: editValue(d); break;
      case kScLoad: case kScSave: mBank = juce::jlimit(0, 7, mBank + d); break;
      case kScLim: if (mSub == 0) bump(T.limPos, d, 1, 28); else bump(T.limNeg, d, 1, 28); break;
      default: break;
    }
  }

  void menuEdit(int it, int d)
  {
    using namespace kr106;
    MxTrack& T = mSeq.mPat.trk[mTrk];
    switch (it)
    {
      case 0: bumpS(T.transpose, d, -60, 60); break;
      case 1: bump(T.sliderOct, d, 1, 4); break;
      case 2: T.sliderDir.set(d > 0 ? 1 : 0); break;
      case 3: bump(T.pulseOvr, d, 0, 8); break;
      case 4: bump(T.gateTypeOvr, d, 0, 6); break;
      case 5: T.stretch.set(d > 0 ? 1 : 0); break;
      case 6: bump(T.ratchMode, d, 0, 2); break;
      case 7: bump(T.slideType, d, 0, 2); break;
      case 8: T.accMode.set(d > 0 ? 1 : 0); break;
      case 9: T.accOrder.set(d > 0 ? 1 : 0); break;
      case 10: T.accPolar.set(d > 0 ? 1 : 0); break;
      case 11: bump(T.limPos, d, 1, 28); break;
      case 12: bump(T.limNeg, d, 1, 28); break;
      case 13: T.mute.set(d > 0 ? 1 : 0); break;
      default: T.restPitch.set(d > 0 ? 1 : 0); break;
    }
  }

  void editValue(int d)
  {
    using namespace kr106;
    MxStage& S = mSeq.mPat.trk[mTrk].st[mEditStage];
    switch (mEditLane)
    {
      case 0: S.slide.set(d > 0 ? 1 : 0); break;
      case 1: S.skip.set(d > 0 ? 1 : 0); break;
      case 2: bump(S.pitchOvr, d, 0, 61); break;
      case 3: bump(S.gateOvr, d, 0, 101); break;
      case 4: bump(S.ratch, d, 1, 8); break;
      case 5: bump(S.prob, d * 5, 0, 100); break;
      case 6: bumpS(S.accAmt, d, -7, 7); break;
      default: bumpS(S.cvVal, d * 4, -127, 127); break;
    }
  }

  void encoderPress()
  {
    using namespace kr106;
    MxTrack& T = mSeq.mPat.trk[mTrk];
    MxLane& L = mSeq.mPat.lane[mLane];
    switch (mScreen)
    {
      case kScOrder: if (mMod) L.dir.set(L.dir.get() ? 0 : 1); else T.dir.set(T.dir.get() ? 0 : 1); break;
      case kScLen: mSub = (mSub + 1) % (mMod ? 2 : 3); break;
      case kScDiv: mSub = mMod ? 0 : (mSub + 1) % 2; break;
      case kScSlide: case kScScale: case kScLim: mSub = (mSub + 1) % 2; break;
      case kScGate: mSub = (mSub + 1) % 3; break;
      case kScBpm: mSeq.mSync.store(!mSeq.mSync.load()); break;
      case kScMenu: mMenuEditing = !mMenuEditing; break;
      case kScEdit:
      {
        MxStage& S = T.st[mEditStage];
        if (mEditLane == 5) S.probTarget.set(S.probTarget.get() ? 0 : 1);
        else if (mEditLane == 6) S.accTrig.set((S.accTrig.get() + 1) % 3);
        else if (mEditLane == 7) S.cvRamp.set((S.cvRamp.get() + 1) % 3);
        else if (mEditLane == 0) S.slide.set(S.slide.get() ? 0 : 1);
        else if (mEditLane == 1) S.skip.set(S.skip.get() ? 0 : 1);
        break;
      }
      case kHome:
        if (mMod) { L.type.set((L.type.get() + 1) % 3); }
        break;
      default: break;
    }
  }

  // ---- sliders ----
  bool sliderAt(juce::Point<int> p, int& band, int& col) const
  {
    for (int i = 0; i < 8; i++)
    {
      if (std::abs(p.x - sx(i)) > 22) continue;
      if (p.y >= kPitchY0 - 12 && p.y <= kPitchY1 + 10) { band = 0; col = i; return true; }
      if (p.y >= kPulseY0 - 10 && p.y <= kPulseY1 + 10) { band = 1; col = i; return true; }
      if (p.y >= kGateY0 - 10 && p.y <= kGateY1 + 10) { band = 2; col = i; return true; }
    }
    return false;
  }

  void setSlider(int band, int col, int y)
  {
    using namespace kr106;
    MxPattern& P = mSeq.mPat;
    auto frac = [&](int y0, int y1) { return juce::jlimit(0.f, 1.f, static_cast<float>(y1 - y) / static_cast<float>(y1 - y0)); };
    if (band == 0)
    {
      const float f = frac(kPitchY0, kPitchY1);
      if (mMod) s8set(P.lane[mLane].val[col], juce::roundToInt(f * 255.f) - 128); else P.pitch[col].set(juce::roundToInt(f * 127.f));
    }
    else if (band == 1)
    {
      const float f = frac(kPulseY0, kPulseY1);
      if (mMod) P.lane[mLane].ramp[col].set(f > 0.5f ? 1 : 0); else P.pulse[col].set(1 + juce::roundToInt(f * 7.f));
    }
    else
    {
      const float f = frac(kGateY0, kGateY1);
      if (mMod) P.lane[mLane].gate[col].set(f > 0.5f ? 1 : 0); else P.gtype[col].set(juce::roundToInt((1.f - f) * 3.f));
    }
  }

  // ---- stage buttons ----
  void stageDown(int i)
  {
    using namespace kr106;
    if (mAlt)
    {
      switch (i)
      {
        case 0: mScreen = kScLoad; break;
        case 1: mScreen = kScSave; break;
        case 6: mScreen = kScLim; mSub = 0; break;
        default: mScreen = kScInfo; mInfo = i == 2 ? 0 : i == 3 ? 1 : i == 4 ? 2 : 3; break;
      }
      mAlt = false;
      return;
    }
    if (mScreen == kScLoad || mScreen == kScSave)
    {
      const int preset = mBank * 8 + i;
      if (mScreen == kScLoad) mSeq.Recall(preset); else mSeq.Save(preset);
      mScreen = kHome;
      return;
    }
    if (mEdit && mEditPick)
    {
      mEditLane = i; mEditPick = false; mScreen = kScEdit; mEditStage = juce::jmax(0, static_cast<int>(mSeq.mPos[mTrk].load()));
      return;
    }
    if (mEdit)
    {
      mEditStage = i;
      MxStage& S = mSeq.mPat.trk[mTrk].st[i];
      if (mEditLane == 0) S.slide.set(S.slide.get() ? 0 : 1);
      else if (mEditLane == 1) S.skip.set(S.skip.get() ? 0 : 1);
      mScreen = kScEdit;
      return;
    }
    if (mLoopy)
    {
      mHeldMask |= 1 << i;
      if (mLoopFirst < 0) { mLoopFirst = i; mSeq.SetLoop(mTrk, i, -1); }
      else mSeq.SetLoop(mTrk, mLoopFirst, i);
      return;
    }
    mSeq.mAudition[mTrk].store(static_cast<int8_t>(i));                  // stage player
  }

  void stageUp(int i)
  {
    if (mLoopy)
    {
      mHeldMask &= ~(1 << i);
      if (mHeldMask == 0) { mLoopFirst = -1; mSeq.SetLoop(mTrk, -1, -1); }
      return;
    }
    if (!mEdit && !mAlt) mSeq.mAudition[mTrk].store(-1);
  }

  bool isLit(int id, juce::uint32 now) const
  {
    if (mPressed == id && id != kEncoder) return true;
    switch (id)
    {
      case kTrk1: return !mMod && mTrk == 0;   case kTrk2: return !mMod && mTrk == 1;   case kMod: return mMod;
      case kRun: return mSeq.mRun.load();      case kLoopy: return mLoopy;
      case kAlt: return mAlt;                  case kEdit: return mEdit;
      case kClk: return mSeq.mSync.load();
      case kLed1: return mSeq.mHigh[0].load() != 0;   case kLed2: return mSeq.mHigh[1].load() != 0;
      case kOrder: return mScreen == kScOrder; case kLen: return mScreen == kScLen;     case kDiv: return mScreen == kScDiv;
      case kSwing: return mScreen == kScSwing; case kSlide: return mScreen == kScSlide; case kGateLen: return mScreen == kScGate;
      case kBpm: return mScreen == kScBpm;     case kScale: return mScreen == kScScale; case kSetup: return mScreen == kScMenu;
      case kReset: return now < mFlashUntil;
      default: return false;
    }
  }

  void press(int id)
  {
    using namespace kr106;
    auto open = [&](int screen) { mScreen = screen; mSub = 0; mAlt = false; mEdit = false; mEditPick = false; };
    switch (id)
    {
      case kTrk1: mMod = false; mTrk = 0; break;
      case kTrk2: mMod = false; mTrk = 1; break;
      case kMod:  mMod = true; if (mScreen != kScOrder && mScreen != kScLen && mScreen != kScDiv) mScreen = kHome; break;
      case kRun:  mSeq.mRun.store(!mSeq.mRun.load()); break;
      case kLoopy: mLoopy = !mLoopy; if (!mLoopy) { mHeldMask = 0; mLoopFirst = -1; mSeq.SetLoop(mTrk, -1, -1); } break;
      case kReset: case kRstJack: mSeq.RequestReset(); mFlashUntil = juce::Time::getMillisecondCounter() + 150; break;
      case kClk: mSeq.mSync.store(!mSeq.mSync.load()); break;
      case kSetup: open(kScMenu); break;
      case kBpm: open(kScBpm); break;
      case kScale: open(kScScale); break;
      case kOrder: open(kScOrder); break;
      case kLen: open(kScLen); break;
      case kDiv: open(kScDiv); break;
      case kSwing: if (!mMod) open(kScSwing); break;
      case kSlide: if (!mMod) open(kScSlide); break;
      case kGateLen: if (!mMod) open(kScGate); break;
      case kAlt: mAlt = !mAlt; if (mAlt) { mEdit = false; mEditPick = false; } break;
      case kEdit:
        if (!mEdit) { mEdit = true; mEditPick = true; mAlt = false; mMod = false; } else mEditPick = true;
        break;
      case kExit: mScreen = kHome; mSub = 0; mEdit = false; mEditPick = false; mAlt = false; mMenuEditing = false; break;
      default: break;
    }
  }
};
