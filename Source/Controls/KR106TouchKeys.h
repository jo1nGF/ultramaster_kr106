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
  void setActive(bool a) { mActive = a; repaint(); }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(1.f);
    g.setColour(mActive ? juce::Colour(0xff9a0e0e) : (mDown ? mFill.brighter(0.4f) : mFill));
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
  bool mDown = false, mActive = false;
};

// ---------------------------------------------------------------------------
// Two-line selector button: small caption on top, current value below.
// Used for the arpeggiator pattern and S&H cycle (tap opens a list).
class KR106TouchChoice : public juce::Component
{
public:
  KR106TouchChoice(const juce::String& caption, std::function<juce::String()> valueText,
                   std::function<void()> onClick)
    : mCaption(caption), mValue(std::move(valueText)), mOnClick(std::move(onClick)) {}

  void setEnabledLook(bool e) { if (e != mEnabledLook) { mEnabledLook = e; repaint(); } }
  juce::String currentText() const { return mValue ? mValue() : juce::String(); }
  void refresh() { repaint(); }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(0.75f);
    g.setColour(mDown ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff262626));
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colour(0xff14388f));
    g.drawRoundedRectangle(r, 4.f, 1.f);
    g.setColour(juce::Colours::white.withAlpha(mEnabledLook ? 0.65f : 0.3f));
    g.setFont(juce::FontOptions(8.5f));
    g.drawText(mCaption, getLocalBounds().withTrimmedTop(2).withHeight(11), juce::Justification::centred);
    g.setColour(juce::Colours::white.withAlpha(mEnabledLook ? 1.f : 0.45f));
    g.setFont(juce::FontOptions(11.f));
    // squeeze long values (e.g. "Ping-pong") instead of cutting them with "..."
    g.drawFittedText(currentText(), getLocalBounds().withTrimmedTop(12).reduced(2, 0),
                     juce::Justification::centred, 1, 0.6f);
  }
  void mouseDown(const juce::MouseEvent&) override { mDown = true; repaint(); }
  void mouseUp(const juce::MouseEvent& e) override
  {
    mDown = false; repaint();
    if (getLocalBounds().contains(e.getPosition()) && mOnClick) mOnClick();
  }

private:
  juce::String mCaption;
  std::function<juce::String()> mValue;
  std::function<void()> mOnClick;
  bool mDown = false, mEnabledLook = true;
};

// ---------------------------------------------------------------------------
// Small on/off toggle with an LED, sized to sit in a tile header (e.g. "Loop").
class KR106TouchToggle : public juce::Component
{
public:
  KR106TouchToggle(const juce::String& text, juce::RangedAudioParameter* param)
    : mText(text), mParam(param) {}

  bool isOn() const { return mParam != nullptr && mParam->getValue() > 0.5f; }
  void refresh() { repaint(); }

  void paint(juce::Graphics& g) override
  {
    const bool on = isOn();
    auto r = getLocalBounds().toFloat().reduced(0.75f);
    g.setColour(mDown ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff1d1d1d));
    g.fillRoundedRectangle(r, 3.5f);
    g.setColour(on ? juce::Colour(0xffff5a4a) : juce::Colour(0xff7a7a7a));
    g.drawRoundedRectangle(r, 3.5f, 1.f);
    // LED
    const float cy = r.getCentreY();
    const float lx = r.getX() + 8.f;
    if (on)
    {
      g.setColour(juce::Colour(0x55ff3020));
      g.fillEllipse(lx - 5.f, cy - 5.f, 10.f, 10.f);
    }
    g.setColour(on ? juce::Colour(0xffff3b2a) : juce::Colour(0xff4a1414));
    g.fillEllipse(lx - 3.f, cy - 3.f, 6.f, 6.f);
    g.setColour(juce::Colours::white.withAlpha(on ? 1.f : 0.75f));
    g.setFont(juce::FontOptions(11.f, on ? juce::Font::bold : juce::Font::plain));
    g.drawText(mText, getLocalBounds().withTrimmedLeft(15), juce::Justification::centred);
  }
  void mouseDown(const juce::MouseEvent&) override { mDown = true; repaint(); }
  void mouseUp(const juce::MouseEvent& e) override
  {
    mDown = false;
    if (mParam != nullptr && getLocalBounds().contains(e.getPosition()))
    {
      mParam->beginChangeGesture();
      mParam->setValueNotifyingHost(isOn() ? 0.f : 1.f);
      mParam->endChangeGesture();
    }
    repaint();
  }

private:
  juce::String mText;
  juce::RangedAudioParameter* mParam;
  bool mDown = false;
};

// ---------------------------------------------------------------------------
// Segmented tabs drawn on a tile header (e.g. "Tape | Digital").
class KR106TouchTabs : public juce::Component
{
public:
  KR106TouchTabs(juce::StringArray names, std::function<void(int)> onChange)
    : mNames(std::move(names)), mOnChange(std::move(onChange)) {}

  void setSelected(int i) { mSel = i; repaint(); }
  int getSelected() const { return mSel; }

  void paint(juce::Graphics& g) override
  {
    const int n = mNames.size();
    if (n == 0) return;
    const float w = (float) getWidth() / (float) n;
    for (int i = 0; i < n; i++)
    {
      juce::Rectangle<float> r(i * w + 1.f, 1.f, w - 2.f, (float) getHeight() - 2.f);
      if (i == mSel)
      {
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.fillRoundedRectangle(r, 3.f);
      }
      g.setColour(juce::Colours::white.withAlpha(i == mSel ? 1.f : 0.6f));
      g.setFont(juce::FontOptions(13.f, i == mSel ? juce::Font::bold : juce::Font::plain));
      g.drawText(mNames[i], r.toNearestInt(), juce::Justification::centred);
    }
  }
  void mouseUp(const juce::MouseEvent& e) override
  {
    if (!getLocalBounds().contains(e.getPosition()) || mNames.isEmpty()) return;
    int i = juce::jlimit(0, mNames.size() - 1, e.x * mNames.size() / juce::jmax(1, getWidth()));
    if (i != mSel) { mSel = i; repaint(); if (mOnChange) mOnChange(i); }
  }

private:
  juce::StringArray mNames;
  std::function<void(int)> mOnChange;
  int mSel = 0;
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
    if (it->second != kIgnoredFinger) release(it->second);
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
    if (it != mFingers.end() && it->second == kIgnoredFinger) return;     // finger that just released a latched key
    if (it != mFingers.end() && it->second == note) return;
    const bool firstTouch = (it == mFingers.end());
    if (it != mFingers.end()) { release(it->second); mFingers.erase(it); }

    // Hold on + a fresh tap on a key that is already latched (lit): release it
    // instead of re-triggering it. Same behaviour as clicking a held key on the
    // desktop keyboard. Dragging onto a latched key still just plays it.
    if (firstTouch && note >= 0 && mProc != nullptr && mCount[note] == 0
        && mProc->getParam(kHold)->getValue() > 0.5f
        && mProc->mKeyboardHeld.test(static_cast<size_t>(note)))
    {
      mProc->forceReleaseNote(note);
      mFingers[idx] = kIgnoredFinger;
      repaint();
      return;
    }
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

  static constexpr int kIgnoredFinger = -2;   // marks a finger used only to release a latched key
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
