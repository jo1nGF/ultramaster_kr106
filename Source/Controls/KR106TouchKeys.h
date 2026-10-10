#pragma once

// On-screen keys for testing the sound when no MIDI controller is attached.
// Shown as a bottom sheet from the "Keys" button (touch UI only).
// Multi-touch polyphony, glissando while dragging, velocity from touch height,
// octave shift, and an optional pitch-bender lever.

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include "../PluginProcessor.h"

// ---------------------------------------------------------------------------
class KR106TouchTextButton : public juce::Component
{
public:
  KR106TouchTextButton(const juce::String& text, std::function<void()> onClick,
                       juce::Colour fill = juce::Colour(0xff2a2a2a))
    : mText(text), mOnClick(std::move(onClick)), mFill(fill) {}

  void setText(const juce::String& t) { mText = t; repaint(); }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(mDown ? mFill.brighter(0.4f) : mFill);
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colour(0xff8a8a8a));
    g.drawRoundedRectangle(r, 4.f, 1.f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.f));
    g.drawText(mText, getLocalBounds(), juce::Justification::centred);
  }
  void mouseDown(const juce::MouseEvent&) override { mDown = true; repaint(); }
  void mouseUp(const juce::MouseEvent& e) override
  {
    mDown = false; repaint();
    if (getLocalBounds().contains(e.getPosition()) && mOnClick) mOnClick();
  }

private:
  juce::String mText;
  std::function<void()> mOnClick;
  juce::Colour mFill;
  bool mDown = false;
};

// ---------------------------------------------------------------------------
class KR106TouchKeys : public juce::Component, private juce::Timer
{
public:
  static constexpr int kOctaves = 3;
  static constexpr int kWhiteKeys = 7 * kOctaves;

  explicit KR106TouchKeys(KR106AudioProcessor* p) : mProc(p) { startTimerHz(20); }
  ~KR106TouchKeys() override { releaseAll(); }

  int getStartNote() const { return mStart; }
  void shiftOctave(int dir)
  {
    releaseAll();
    mStart = juce::jlimit(24, 84, mStart + 12 * dir);
    repaint();
  }

  void releaseAll()
  {
    for (int n = 0; n < 128; n++)
      if (mCount[n] > 0) { mCount[n] = 0; send(0x80, n, 0); }
    mFingers.clear();
  }

  void paint(juce::Graphics& g) override
  {
    const float w = (float) getWidth(), h = (float) getHeight();
    const float kw = w / (float) kWhiteKeys;
    g.fillAll(juce::Colour(0xff0a0a0a));

    auto sounding = [this](int note) {
      return mCount[note] > 0 || (mProc && mProc->mKeyboardHeld.test((size_t) note));
    };

    // white keys
    for (int i = 0; i < kWhiteKeys; i++)
    {
      int note = whiteNote(i);
      juce::Rectangle<float> r(i * kw + 0.5f, 0.f, kw - 1.f, h);
      g.setColour(sounding(note) ? juce::Colour(0xfff0ad38) : juce::Colour(0xfff2efe6));
      g.fillRoundedRectangle(r, 3.f);
      if (note % 12 == 0)
      {
        g.setColour(juce::Colour(0xff555555));
        g.setFont(juce::FontOptions(12.f));
        g.drawText("C" + juce::String(note / 12 - 1), r.withTrimmedTop(h - 22.f),
                   juce::Justification::centred);
      }
    }
    // black keys
    for (int i = 0; i < kWhiteKeys; i++)
    {
      if (!hasBlackAfter(i)) continue;
      int note = whiteNote(i) + 1;
      auto r = blackRect(i, kw, h);
      g.setColour(sounding(note) ? juce::Colour(0xffc60a0a) : juce::Colour(0xff161616));
      g.fillRoundedRectangle(r, 2.f);
    }
  }

  void mouseDown(const juce::MouseEvent& e) override { press(e); }
  void mouseDrag(const juce::MouseEvent& e) override { press(e); }
  void mouseUp(const juce::MouseEvent& e) override
  {
    auto it = mFingers.find(e.source.getIndex());
    if (it == mFingers.end()) return;
    release(it->second);
    mFingers.erase(it);
    repaint();
  }

private:
  void timerCallback() override { if (isShowing()) repaint(); }

  // Called for both down and drag: moves the finger's note if it changed.
  void press(const juce::MouseEvent& e)
  {
    int idx = e.source.getIndex();
    int note = hitTest(e.position);
    auto it = mFingers.find(idx);
    if (it != mFingers.end() && it->second == note) return;
    if (it != mFingers.end()) { release(it->second); mFingers.erase(it); }
    if (note >= 0)
    {
      float y = juce::jlimit(0.f, 1.f, e.position.y / (float) getHeight());
      int vel = 50 + (int) (77.f * y);          // touch lower on key = louder
      if (mCount[note]++ == 0) send(0x90, note, vel);
      mFingers[idx] = note;
    }
    repaint();
  }
  void release(int note)
  {
    if (note >= 0 && note < 128 && mCount[note] > 0 && --mCount[note] == 0)
      send(0x80, note, 0);
  }
  void send(int status, int note, int vel)
  {
    if (mProc) mProc->sendMidiFromUI((uint8_t) status, (uint8_t) note, (uint8_t) vel);
  }

  static bool hasBlackAfter(int whiteIdx) { int d = whiteIdx % 7; return d != 2 && d != 6; }
  int whiteNote(int whiteIdx) const
  {
    static const int off[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return mStart + (whiteIdx / 7) * 12 + off[whiteIdx % 7];
  }
  juce::Rectangle<float> blackRect(int whiteIdx, float kw, float h) const
  {
    float bw = kw * 0.62f;
    return { (whiteIdx + 1) * kw - bw * 0.5f, 0.f, bw, h * 0.6f };
  }
  int hitTest(juce::Point<float> p) const
  {
    const float kw = (float) getWidth() / (float) kWhiteKeys, h = (float) getHeight();
    if (p.x < 0 || p.y < 0 || p.x >= (float) getWidth() || p.y >= h) return -1;
    for (int i = 0; i < kWhiteKeys; i++)
      if (hasBlackAfter(i) && blackRect(i, kw, h).contains(p)) return whiteNote(i) + 1;
    int i = juce::jlimit(0, kWhiteKeys - 1, (int) (p.x / kw));
    return whiteNote(i);
  }

  KR106AudioProcessor* mProc;
  int mStart = 48;                       // C3
  int mCount[128] = {};                  // fingers holding each note
  std::map<int, int> mFingers;           // touch source index -> note
};

// ---------------------------------------------------------------------------
// Bottom sheet: title row (octave -/+, bender, close) + keys.
class KR106TouchKeysSheet : public juce::Component
{
public:
  std::function<void()> onClose;

  KR106TouchKeysSheet(KR106AudioProcessor* p, juce::Component* benderOwnedByUs)
    : mKeys(p), mBender(benderOwnedByUs)
  {
    addAndMakeVisible(mKeys);
    mDown = std::make_unique<KR106TouchTextButton>("Oct -", [this] { mKeys.shiftOctave(-1); updateRange(); });
    mUp   = std::make_unique<KR106TouchTextButton>("Oct +", [this] { mKeys.shiftOctave(+1); updateRange(); });
    mClose = std::make_unique<KR106TouchTextButton>("Close", [this] { if (onClose) onClose(); }, juce::Colour(0xff9a0e0e));
    addAndMakeVisible(*mDown); addAndMakeVisible(*mUp); addAndMakeVisible(*mClose);
    if (mBender) addAndMakeVisible(mBender.get());
    updateRange();
  }

  void paint(juce::Graphics& g) override
  {
    g.fillAll(juce::Colour(0xff1b1b1b));
    g.setColour(juce::Colour(0xffc60a0a));
    g.fillRect(0, 0, getWidth(), 2);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.f));
    g.drawText(mRange, 72, 4, 110, 30, juce::Justification::centred);
    g.setColour(juce::Colour(0xff9a9a9a));
    g.drawText("Test keys", getWidth() / 2 - 60, 4, 120, 30, juce::Justification::centred);
  }

  void resized() override
  {
    const int pad = 8, hdr = 36;
    mDown->setBounds(pad, 5, 60, 28);
    mUp->setBounds(pad + 178, 5, 60, 28);
    mClose->setBounds(getWidth() - pad - 70, 5, 70, 28);
    if (mBender)   // native 60x12, drawn 2.2x (132x26); transform includes the position
    {
        mBender->setBounds(0, 0, 60, 12);
        mBender->setTransform(juce::AffineTransform::scale(2.2f).translated((float) getWidth() - 330.f, 8.f));
    }
    mKeys.setBounds(pad, hdr + 4, getWidth() - 2 * pad, getHeight() - hdr - 4 - pad);
  }

  // Swipe down on the header closes the sheet.
  void mouseDown(const juce::MouseEvent& e) override { mSwipeStart = e.position.y; }
  void mouseUp(const juce::MouseEvent& e) override
  {
    if (mSwipeStart >= 0 && e.position.y - mSwipeStart > 40.f && onClose) onClose();
    mSwipeStart = -1.f;
  }

private:
  void updateRange()
  {
    int s = mKeys.getStartNote();
    mRange = "C" + juce::String(s / 12 - 1) + " - C" + juce::String((s + 36) / 12 - 1);
    repaint();
  }
  KR106TouchKeys mKeys;
  std::unique_ptr<juce::Component> mBender;
  std::unique_ptr<KR106TouchTextButton> mDown, mUp, mClose;
  juce::String mRange;
  float mSwipeStart = -1.f;
};
