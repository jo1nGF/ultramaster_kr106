#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KR106Tooltip.h"
#include "KR106Touch.h"
#include "PluginProcessor.h"

// Pixel-perfect vertical fader — port of KR106SliderControl from iPlug2.
// 13×49 draw area: white/gray tick marks, black well, gray thumb.
class KR106Slider : public juce::Component
{
public:
  KR106Slider(juce::RangedAudioParameter* param, KR106Tooltip* tip = nullptr,
              const juce::Image& handleImg = {})
    : mParam(param), mTooltip(tip), mHandleImg(handleImg) {}

  void setMidiLearn(KR106AudioProcessor* proc, int paramIdx)
  { mProcessor = proc; mParamIdx = paramIdx; }

  // Extra right-side pixel for tick marks (fills gap to adjacent slider)
  void setExtraRight(int px) { mExtraRight = px; }

  // Override to customize tick marks. Default: 11 evenly spaced marks,
  // lines 0/5/10 bright, others dim.
  virtual void paintTickMarks(juce::Graphics& g)
  {
    auto bright = juce::Colour(219, 219, 219);
    auto dim    = juce::Colour(126, 126, 126);
    float tw = 17.f + static_cast<float>(mExtraRight);
    for (int i = 0; i <= 10; i++)
    {
      float y = std::round(44.f - i * 4.f);
      g.setColour((i == 0 || i == 5 || i == 10) ? bright : dim);
      g.fillRect(1.f, y, tw, 1.f);
    }
  }

  // Touch UI: smooth vector groove + handle (the 1x bitmap handle gets soft when scaled up)
  void paintVectorFader(juce::Graphics& g)
  {
    paintTickMarks(g);
    // groove
    auto well = juce::Rectangle<float>(7.f, 0.5f, 5.f, 48.f);
    g.setColour(juce::Colour(0xff060606));
    g.fillRoundedRectangle(well, 2.4f);
    g.setColour(juce::Colour(0xff303030));
    g.drawRoundedRectangle(well, 2.4f, 0.6f);
    g.setColour(juce::Colour(0xff000000));
    g.fillRect(9.f, 2.f, 1.f, 45.f);

    // handle
    // the handle follows the matrix while a modulation source drives this control
    const float val = juce::jlimit(0.f, 1.f, (mProcessor && mParamIdx >= 0) ? mProcessor->modDisplayValue(mParamIdx)
                                                                          : (mParam ? mParam->getValue() : 0.f));
    const float cy = 44.f - val * 40.f + 1.f;
    juce::Rectangle<float> h(2.f, cy - 4.5f, 15.f, 9.f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(h.translated(0.f, 1.2f), 2.f);
    juce::ColourGradient body(juce::Colour(0xfff6f6f6), h.getX(), h.getY(),
                              juce::Colour(0xff9c9c9c), h.getX(), h.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(h, 2.f);
    g.setColour(juce::Colour(0xff0c0c0c));
    g.drawRoundedRectangle(h, 2.f, 0.9f);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(h.getX() + 1.8f, cy - 0.3f, h.getWidth() - 3.6f, 0.7f);     // centre groove
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.fillRect(h.getX() + 1.8f, cy - 1.3f, h.getWidth() - 3.6f, 0.6f);     // highlight
    paintMidiLearnBorder(g);
  }

  void paint(juce::Graphics& g) override
  {
    if constexpr (kr106touch::kEnabled) { paintVectorFader(g); return; }
    paintTickMarks(g);

    auto hLine = [&](juce::Colour c, float x1, float y, float x2) {
      g.setColour(c); g.fillRect(x1, y, x2 - x1, 1.f);
    };
    auto vLine = [&](juce::Colour c, float x, float y1, float y2) {
      g.setColour(c); g.fillRect(x, y1, 1.f, y2 - y1);
    };
    auto pixelRect = [&](juce::Colour c, float l, float t, float r, float b) {
      hLine(c, l, t, r); hLine(c, l, b - 1.f, r);
      vLine(c, l, t, b); vLine(c, r - 1.f, t, b);
    };

    const auto white = juce::Colour(255, 255, 255);
    const auto black = juce::Colour(0, 0, 0);
    const auto dark  = juce::Colour(34, 34, 34);

    // Well (centered in 19px: offset 3px from 13px layout)
    static constexpr float kOfs = 3.f;
 
    g.setColour(dark);
    g.fillRect(4.f + kOfs, 1.f, 5.f, 47.f);
    hLine(dark, 5 + kOfs, 0, 8 + kOfs);  
    hLine(dark, 5 + kOfs, 48, 8 + kOfs);
    g.setColour(black);
    g.fillRect(6.f + kOfs, 2.f, 1.f, 45.f);

    // Handle
    float val = mParam ? mParam->getValue() : 0.f;
    float fy = std::round(44.f - val * 40.f);

    if (mHandleImg.isValid())
    {
      float hw = mHandleImg.getWidth() / 2.f;
      float hh = mHandleImg.getHeight() / 2.f;
      float hx = std::floor((19.f - hw) * 0.5f);
      g.drawImage(mHandleImg,
                  hx, fy - hh * 0.5f + 1.f, hw, hh,
                  0, 0, mHandleImg.getWidth(), mHandleImg.getHeight());
    }
    else
    {
      pixelRect(black, 1.f + kOfs, fy - 2, 12.f + kOfs, fy + 3);
      hLine(dark, 2 + kOfs, fy - 1, 11 + kOfs);
      hLine(dark, 2 + kOfs, fy + 1, 11 + kOfs);
      hLine(white, 2 + kOfs, fy, 11 + kOfs);
    }

    paintMidiLearnBorder(g);
  }

  void paintMidiLearnBorder(juce::Graphics& g)
  {
    if (mProcessor && mParamIdx >= 0
        && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
    {
      g.setColour(juce::Colour(0, 255, 0));
      g.drawRect(0, 0, getWidth() - mExtraRight, getHeight(), 1);
    }
  }

  void mouseEnter(const juce::MouseEvent&) override
  {
    if (mTooltip && !mDragging && !mEditor)
    {
      updateCCLine();
      mTooltip->show(mParam, this);
    }
  }

  void mouseExit(const juce::MouseEvent&) override
  {
    if (mTooltip && !mDragging) mTooltip->hide();
  }

  void mouseMove(const juce::MouseEvent& e) override
  {
    applyCursorWorkaround(e);
  }

  void mouseDown(const juce::MouseEvent& e) override
  {
    if (e.mods.isPopupMenu() && mProcessor && mParamIdx >= 0)
    {
      handleMidiLearnClick();
      return;
    }
    // Left click cancels MIDI learn if active, then falls through to start drag
    if (mProcessor && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) >= 0)
    {
      mProcessor->cancelMidiLearn();
      if (mTooltip) mTooltip->hide();
      repaint();
    }
    mDragging = true;
    mRightDrag = false;
    mAccumVal = mParam ? mParam->getValue() : 0.f;
    mLastRawDY = 0.f;
    if (mParam) mParam->beginChangeGesture();
    if (mTooltip) mTooltip->show(mParam, this);
    setMouseCursor(juce::MouseCursor::NoCursor);
    e.source.enableUnboundedMouseMovement(true);
    if (isAppleHost()) mCursorDirty = true;
  }

  void mouseDrag(const juce::MouseEvent& e) override
  {
    applyCursorWorkaround(e);
    if (!mParam) return;
    float dy = static_cast<float>(e.getOffsetFromDragStart().y);
    float increment = dy - mLastRawDY;
    mLastRawDY = dy;

    // Cumulative gearing: shift only affects new movement
    // Scale by display DPI so non-retina screens get the same physical throw
    float dpiScale = std::max(1.f, static_cast<float>(getTopLevelComponent()->getDesktopScaleFactor()));
    float gearing = kr106touch::kEnabled ? kr106touch::kSliderThrow : 127.f;
    if (e.mods.isCommandDown()) gearing *= 100.f;       // Cmd/Ctrl = superfine
    else if (e.mods.isShiftDown()) gearing *= 10.f;     // Shift = fine
    if (!kr106touch::kEnabled) gearing /= dpiScale;
    mAccumVal = juce::jlimit(0.f, 1.f, mAccumVal + -increment / gearing);
    float newVal = mAccumVal;
    mParam->setValueNotifyingHost(newVal);
    if (mTooltip) mTooltip->update();
    repaint();
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    if (!mDragging) return; // right-click MIDI learn, no drag to clean up
    mDragging = false;
    if (mParam) mParam->endChangeGesture();
    if (mTooltip) mTooltip->hide();
    e.source.enableUnboundedMouseMovement(false);
    setMouseCursor(juce::MouseCursor::NormalCursor);

    // Warp cursor to the handle so it appears on the control after release
    float val = mParam ? mParam->getValue() : 0.f;
    float fy = std::round(44.f - val * 40.f);
    auto screenPos = localPointToGlobal(juce::Point<int>(9, static_cast<int>(fy)));
    juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(screenPos.toFloat());
    if (isAppleHost()) mCursorDirty = true;
  }

  void mouseDoubleClick(const juce::MouseEvent&) override
  {
    if (!mParam) return;
    // Integer/bool controls keep their click semantics; continuous faders reset.
    if (dynamic_cast<juce::AudioParameterBool*>(mParam)) return;
    if (dynamic_cast<juce::AudioParameterInt*>(mParam)) return;
    if (mEditor) dismissEdit();
    if (mTooltip) mTooltip->hide();
    const float def = mParam->getDefaultValue(); // normalized default, respects each parameter's range
    mParam->beginChangeGesture();
    mParam->setValueNotifyingHost(def);
    mParam->endChangeGesture();
    mAccumVal = def;
    mLastRawDY = 0.f;
    repaint();
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    // Snap on release too for clean final position
    if (mSynthModel && *mSynthModel != kr106::kJ6 && mParam)
    {
      float val = mParam->getValue();
      float snapped = std::round(val * 3.f) / 3.f;
      if (snapped != val)
        mParam->setValueNotifyingHost(snapped);
    }
    KR106Slider::mouseUp(e);
  }

private:
  kr106::Model* mSynthModel = nullptr;
};

// Arp Rate slider: when DAW sync is enabled, draws tick marks for note
// divisions and snaps to discrete positions. When sync is off, behaves
// as a normal continuous slider (no tick marks).
class KR106ArpRateSlider : public KR106Slider
{
public:
  KR106ArpRateSlider(juce::RangedAudioParameter* rateParam,
                     juce::RangedAudioParameter* quantizeParam,
                     int rateParamIdx, int quantizeParamIdx,
                     KR106Tooltip* tip,
                     const juce::Image& handleImg, bool* syncFlag)
    : KR106Slider(rateParam, tip, handleImg),
      mRateParam(rateParam), mQuantizeParam(quantizeParam),
      mRateParamIdx(rateParamIdx), mQuantizeParamIdx(quantizeParamIdx),
      mSyncFlag(syncFlag) {}

  // Call from OnIdle to swap the active param when sync state changes
  void updateSyncState()
  {
    bool synced = mSyncFlag && *mSyncFlag;
    auto* wanted = synced ? mQuantizeParam : mRateParam;
    if (wanted != mParam)
    {
      mParam = wanted;
      mParamIdx = synced ? mQuantizeParamIdx : mRateParamIdx;
      repaint();
    }
  }

  void paintTickMarks(juce::Graphics& g) override
  {
    if (mSyncFlag && *mSyncFlag)
    {
      auto bright = juce::Colour(219, 219, 219);
      auto dim    = juce::Colour(126, 126, 126);
      // 9 tick marks at division positions (evenly spaced across 40px travel)
      float tw = 17.f + static_cast<float>(mExtraRight);
      for (int i = 0; i < kr106::kNumArpDivisions; i++)
      {
        float norm = static_cast<float>(i) / static_cast<float>(kr106::kNumArpDivisions - 1);
        float y = std::round(44.f - norm * 40.f);
        bool major = (i == 0 || i == kr106::kNumArpDivisions / 2 || i == kr106::kNumArpDivisions - 1);
        g.setColour(major ? bright : dim);
        g.fillRect(1.f, y, tw, 1.f);
      }
    }
    else
    {
      KR106Slider::paintTickMarks(g);
    }
  }

  void mouseDrag(const juce::MouseEvent& e) override
  {
    KR106Slider::mouseDrag(e);
    if (mSyncFlag && *mSyncFlag && mParam)
    {
      float val = mParam->getValue();
      float steps = static_cast<float>(kr106::kNumArpDivisions - 1);
      float snapped = std::round(val * steps) / steps;
      if (snapped != val)
      {
        mParam->setValueNotifyingHost(snapped);
        repaint();
      }
    }
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    if (mSyncFlag && *mSyncFlag && mParam)
    {
      float val = mParam->getValue();
      float steps = static_cast<float>(kr106::kNumArpDivisions - 1);
      float snapped = std::round(val * steps) / steps;
      if (snapped != val)
        mParam->setValueNotifyingHost(snapped);
    }
    KR106Slider::mouseUp(e);
  }

private:
  juce::RangedAudioParameter* mRateParam = nullptr;
  juce::RangedAudioParameter* mQuantizeParam = nullptr;
  int mRateParamIdx = -1;
  int mQuantizeParamIdx = -1;
  bool* mSyncFlag = nullptr;
};

// LFO Rate slider: when DAW sync is enabled, draws tick marks for note
// divisions and snaps to discrete positions. Same pattern as ArpRateSlider
// but with 12 LFO divisions (includes Maxima/Longa/Breve).
class KR106LfoRateSlider : public KR106Slider
{
public:
  KR106LfoRateSlider(juce::RangedAudioParameter* rateParam,
                     juce::RangedAudioParameter* quantizeParam,
                     int rateParamIdx, int quantizeParamIdx,
                     KR106Tooltip* tip,
                     const juce::Image& handleImg, bool* syncFlag)
    : KR106Slider(rateParam, tip, handleImg),
      mRateParam(rateParam), mQuantizeParam(quantizeParam),
      mRateParamIdx(rateParamIdx), mQuantizeParamIdx(quantizeParamIdx),
      mSyncFlag(syncFlag) {}

  void updateSyncState()
  {
    bool synced = mSyncFlag && *mSyncFlag;
    auto* wanted = synced ? mQuantizeParam : mRateParam;
    if (wanted != mParam)
    {
      mParam = wanted;
      mParamIdx = synced ? mQuantizeParamIdx : mRateParamIdx;
      repaint();
    }
  }

  void paintTickMarks(juce::Graphics& g) override
  {
    if (mSyncFlag && *mSyncFlag)
    {
      auto bright = juce::Colour(219, 219, 219);
      auto dim    = juce::Colour(126, 126, 126);
      float rw = 17.f + static_cast<float>(mExtraRight);

      // Right side: standard 11 ticks (same as base slider)
      for (int i = 0; i <= 10; i++)
      {
        float y = std::round(44.f - i * 4.f);
        g.setColour((i == 0 || i == 5 || i == 10) ? bright : dim);
        g.fillRect(10.f, y, rw - 9.f, 1.f);
      }

      // Left side: 7 division ticks (every other from 13 divisions)
      int last = kr106::kNumLfoDivisions - 1;
      int mid = last / 2;
      for (int i = 0; i <= last; i += 2)
      {
        float norm = static_cast<float>(i) / static_cast<float>(last);
        float y = std::round(44.f - norm * 40.f);
        bool major = (i == 0 || i == mid || i == last);
        g.setColour(major ? bright : dim);
        g.fillRect(1.f, y, 6.f, 1.f);
      }
    }
    else
    {
      KR106Slider::paintTickMarks(g);
    }
  }

  void mouseDrag(const juce::MouseEvent& e) override
  {
    KR106Slider::mouseDrag(e);
    if (mSyncFlag && *mSyncFlag && mParam)
    {
      float val = mParam->getValue();
      float steps = static_cast<float>(kr106::kNumLfoDivisions - 1);
      float snapped = std::round(val * steps) / steps;
      if (snapped != val)
      {
        mParam->setValueNotifyingHost(snapped);
        repaint();
      }
    }
  }

  void mouseUp(const juce::MouseEvent& e) override
  {
    if (mSyncFlag && *mSyncFlag && mParam)
    {
      float val = mParam->getValue();
      float steps = static_cast<float>(kr106::kNumLfoDivisions - 1);
      float snapped = std::round(val * steps) / steps;
      if (snapped != val)
        mParam->setValueNotifyingHost(snapped);
    }
    KR106Slider::mouseUp(e);
  }

private:
  juce::RangedAudioParameter* mRateParam = nullptr;
  juce::RangedAudioParameter* mQuantizeParam = nullptr;
  int mRateParamIdx = -1;
  int mQuantizeParamIdx = -1;
  bool* mSyncFlag = nullptr;
};
