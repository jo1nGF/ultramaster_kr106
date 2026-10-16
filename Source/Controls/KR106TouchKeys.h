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
// Horizontal fine slider (bipolar amounts): drag left/right. The full range takes ~300 px,
// so 1 px is about 0.7 % (fine enough for a 86 px wide control). Centre is 0 %, and a
// double tap returns to 0 %. Hold still for MIDI learn (LongPress).
class KR106TouchFineSlider : public juce::Component, private juce::Timer
{
public:
  static constexpr float kThrowPx = 300.f;

  KR106TouchFineSlider(juce::RangedAudioParameter* param, std::function<juce::String(float)> format)
    : mParam(param), mFormat(std::move(format)) { startTimerHz(15); }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(0.75f);
    const float v = mParam ? mParam->getValue() : 0.5f;
    g.setColour(mDragging ? juce::Colour(0xff34343a) : juce::Colour(0xff262626));
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colour(0xff14388f));
    g.drawRoundedRectangle(r, 4.f, 1.f);

    // bar from the centre (0 %) to the value
    const float cx = r.getCentreX();
    const float x = r.getX() + r.getWidth() * v;
    g.setColour(v >= 0.5f ? juce::Colour(0xffc60a0a) : juce::Colour(0xffb8702a));
    g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(cx, x), r.getY() + 3.f,
                                                  std::abs(x - cx), r.getHeight() - 6.f), 2.f);
    // handle and centre detent
    g.setColour(juce::Colour(0xffe4e4e4));
    g.fillRoundedRectangle(juce::Rectangle<float>(x - 2.f, r.getY() + 2.f, 4.f, r.getHeight() - 4.f), 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.fillRect(cx, r.getY() + 1.f, 0.8f, r.getHeight() - 2.f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(11.f));
    g.drawText(mFormat ? mFormat(v) : juce::String(v, 2), getLocalBounds(), juce::Justification::centred);
  }

  void mouseDown(const juce::MouseEvent&) override
  {
    if (!mParam) return;
    mStart = mParam->getValue();
    mParam->beginChangeGesture();
    mDragging = true;
    repaint();
  }
  void mouseDrag(const juce::MouseEvent& e) override
  {
    if (!mParam || !mDragging) return;
    float v = juce::jlimit(0.f, 1.f, mStart + e.getOffsetFromDragStart().x / kThrowPx);
    if (std::abs(v - 0.5f) < 0.004f) v = 0.5f;                  // detent at 0 %
    mParam->setValueNotifyingHost(v);
    repaint();
  }
  void mouseUp(const juce::MouseEvent&) override
  {
    if (mParam && mDragging) mParam->endChangeGesture();
    mDragging = false;
    repaint();
  }
  void mouseDoubleClick(const juce::MouseEvent&) override
  {
    if (!mParam) return;
    mParam->beginChangeGesture();
    mParam->setValueNotifyingHost(0.5f);
    mParam->endChangeGesture();
    repaint();
  }

private:
  void timerCallback() override { if (isShowing()) repaint(); }
  juce::RangedAudioParameter* mParam;
  std::function<juce::String(float)> mFormat;
  float mStart = 0.5f;
  bool mDragging = false;
};

// ---------------------------------------------------------------------------
// LED that shows what a modulation source is sending right now (bipolar LFOs: red when
// positive, blue when negative; unipolar envelope: amber). Brightness = size of the signal.
class KR106ModLed : public juce::Component, private juce::Timer
{
public:
  KR106ModLed(KR106AudioProcessor* p, int source, bool bipolar)
    : mProc(p), mSrc(source), mBipolar(bipolar) { startTimerHz(30); }

  void paint(juce::Graphics& g) override
  {
    const float v = mProc ? mProc->getModSourceValue(mSrc) : 0.f;
    const float a = juce::jlimit(0.f, 1.f, mBipolar ? std::abs(v) : v);
    const juce::Colour on = !mBipolar ? juce::Colour(0xffffa030)
                          : (v < 0.f ? juce::Colour(0xff3a8cff) : juce::Colour(0xffff3b2a));
    auto c = getLocalBounds().toFloat().reduced(2.f);
    g.setColour(juce::Colour(0xff120808));
    g.fillEllipse(c);
    if (a > 0.01f)
    {
      g.setColour(on.withAlpha(0.35f * a));
      g.fillEllipse(c.expanded(1.5f * a));
    }
    g.setColour(on.withAlpha(0.15f + 0.85f * a));
    g.fillEllipse(c.reduced(1.5f));
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(c, 0.8f);
  }

private:
  void timerCallback() override { if (isShowing()) repaint(); }
  KR106AudioProcessor* mProc;
  int mSrc;
  bool mBipolar;
};

// ---------------------------------------------------------------------------
// Horizontal bar meter (gain reduction / level).  value() returns 0..1.
class KR106TouchMeter : public juce::Component, private juce::Timer
{
public:
  KR106TouchMeter(const juce::String& caption, std::function<float()> value, juce::Colour col)
    : mCaption(caption), mValue(std::move(value)), mCol(col) { startTimerHz(25); }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff0b0b0b));
    g.fillRoundedRectangle(r, 3.f);
    const float v = juce::jlimit(0.f, 1.f, mShown);
    if (v > 0.002f)
    {
      g.setColour(mCol);
      g.fillRoundedRectangle(r.withWidth(std::max(4.f, r.getWidth() * v)), 3.f);
    }
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(r, 3.f, 0.8f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::FontOptions(10.f));
    g.drawText(mCaption, getLocalBounds().withTrimmedLeft(4), juce::Justification::centredLeft);
  }

private:
  void timerCallback() override
  {
    if (!isShowing()) return;
    const float v = mValue ? mValue() : 0.f;
    // fast attack, slower fall so short peaks stay visible
    mShown = v > mShown ? v : mShown + (v - mShown) * 0.25f;
    repaint();
  }
  juce::String mCaption;
  std::function<float()> mValue;
  juce::Colour mCol;
  float mShown = 0.f;
};

// ---------------------------------------------------------------------------
// WAV recorder panel: big record button, take time, level meter, last file name.
// Everything is read from kr106touch::hostRecorder() (set by the Android host).
class KR106TouchRecorder : public juce::Component, private juce::Timer
{
public:
  KR106TouchRecorder() { startTimerHz(15); }

  void paint(juce::Graphics& g) override
  {
    auto& h = kr106touch::hostRecorder();
    const bool avail = (bool) h.toggle;
    const bool rec = avail && h.isRecording && h.isRecording();
    const float R = (float) std::min(getHeight(), 66) * 0.5f;
    const juce::Point<float> c(R + 4.f, (float) getHeight() * 0.5f);

    // button
    g.setColour(juce::Colour(0xff0b0b0b));
    g.fillEllipse(c.x - R, c.y - R, 2.f * R, 2.f * R);
    g.setColour(rec ? juce::Colour(0xffff4a3a) : juce::Colour(0xff7a7a7a));
    g.drawEllipse(c.x - R + 1.f, c.y - R + 1.f, 2.f * R - 2.f, 2.f * R - 2.f, 2.f);
    if (rec)
    {
      const bool blink = ((juce::Time::getMillisecondCounter() / 500) & 1) != 0;
      g.setColour(blink ? juce::Colour(0xffff3b2a) : juce::Colour(0xffb02418));
      g.fillRoundedRectangle(c.x - R * 0.34f, c.y - R * 0.34f, R * 0.68f, R * 0.68f, 3.f);   // stop square
    }
    else
    {
      g.setColour(avail ? juce::Colour(0xffe02a1a) : juce::Colour(0xff4a4a4a));
      g.fillEllipse(c.x - R * 0.42f, c.y - R * 0.42f, R * 0.84f, R * 0.84f);                  // record dot
    }

    // time
    const double sec = (avail && h.seconds) ? h.seconds() : 0.0;
    const int m = (int) (sec / 60.0), s = (int) sec % 60, ds = (int) (sec * 10.0) % 10;
    g.setColour(rec ? juce::Colour(0xffff6a5a) : juce::Colours::white);
    g.setFont(juce::FontOptions(24.f, juce::Font::bold));
    const float tx = c.x + R + 12.f;
    g.drawText(juce::String::formatted("%02d:%02d.%d", m, s, ds), (int) tx, 4, getWidth() - (int) tx, 30,
               juce::Justification::centredLeft);

    // level meter
    const float lv = (avail && h.level) ? juce::jlimit(0.f, 1.f, h.level()) : 0.f;
    juce::Rectangle<float> mb(tx, 40.f, (float) getWidth() - tx - 4.f, 10.f);
    g.setColour(juce::Colour(0xff0b0b0b)); g.fillRoundedRectangle(mb, 3.f);
    g.setColour(lv > 0.9f ? juce::Colour(0xffff4a3a) : juce::Colour(0xff4ad66a));
    g.fillRoundedRectangle(mb.withWidth(std::max(2.f, mb.getWidth() * lv)), 3.f);

    // status line
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(juce::FontOptions(10.f));
    juce::String status = !avail ? "recorder not available"
                        : rec ? "recording the master output (24-bit WAV)"
                        : (h.lastName && h.lastName().isNotEmpty() ? "last: " + h.lastName() : "tap the red button to record");
    g.drawText(status, (int) tx, 54, getWidth() - (int) tx, 14, juce::Justification::centredLeft);
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    auto& h = kr106touch::hostRecorder();
    const float R = (float) std::min(getHeight(), 66) * 0.5f;
    const juce::Point<float> c(R + 4.f, (float) getHeight() * 0.5f);
    if (h.toggle && e.position.getDistanceFrom(c) <= R + 6.f) h.toggle();
  }

private:
  void timerCallback() override { if (isShowing()) repaint(); }
};

// ---------------------------------------------------------------------------
// Live view of the Clouds buffer: grains drawn as bars over the 2 s buffer
// (left = oldest, right = newest). Blue outline while frozen.
class KR106CloudsView : public juce::Component, private juce::Timer
{
public:
  explicit KR106CloudsView(KR106AudioProcessor* p) : mProc(p) { startTimerHz(30); }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff08100c));
    g.fillRoundedRectangle(r, 4.f);
    const bool frozen = mViz.frozen;
    g.setColour(frozen ? juce::Colour(0xff4a7aff) : juce::Colour(0xff2a4a34));
    g.drawRoundedRectangle(r, 4.f, frozen ? 2.f : 1.f);
    // time ruler
    g.setColour(juce::Colour(0xff1c3024));
    for (int i = 1; i < 4; i++) g.drawVerticalLine((int) (r.getX() + r.getWidth() * i / 4.f), r.getY() + 3.f, r.getBottom() - 3.f);
    // grains
    for (int i = 0; i < mViz.count; i++)
    {
      const float x = r.getX() + 4.f + (r.getWidth() - 8.f) * juce::jlimit(0.f, 1.f, mViz.pos[i]);
      const float a = juce::jlimit(0.f, 1.f, mViz.amp[i]);
      const float h = (r.getHeight() - 16.f) * (0.15f + 0.85f * a);
      g.setColour(juce::Colour(0xff6affa0).withAlpha(0.25f + 0.75f * a));
      g.fillRoundedRectangle(x - 1.5f, r.getCentreY() - h * 0.5f, 3.f, h, 1.5f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("2 s ago", getLocalBounds().withTrimmedLeft(6).withTrimmedTop(2), juce::Justification::topLeft);
    g.drawText(frozen ? "FROZEN" : "now", getLocalBounds().withTrimmedRight(6).withTrimmedTop(2), juce::Justification::topRight);
    g.drawText(juce::String(mViz.count) + " grains", getLocalBounds().withTrimmedLeft(6).withTrimmedBottom(2), juce::Justification::bottomLeft);
  }

private:
  void timerCallback() override
  {
    if (!isShowing() || mProc == nullptr) return;
    mProc->mDSP.mClouds.GetViz(mViz);
    repaint();
  }
  KR106AudioProcessor* mProc;
  kr106::Clouds::Viz mViz {};
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
