#pragma once

// Touch-screen support (phones / tablets). Enabled with -DKR106_TOUCH=1
// (CMake option KR106_TOUCH). On desktop builds everything here is inert.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>

#ifndef KR106_TOUCH
#define KR106_TOUCH 0
#endif

// The plugin client wrapper normally defines this; the Android app has no wrapper.
#ifndef JucePlugin_VersionString
#define JucePlugin_VersionString "2.5.13"
#endif

namespace kr106touch {

constexpr bool kEnabled = (KR106_TOUCH != 0);

// Drag throw, in *local* control pixels per full 0..1 sweep.
// A fader's drawn travel is ~40 px, so 40 makes the handle follow the finger
// 1:1 regardless of how much the control is scaled up for touch.
constexpr float kSliderThrow = 40.f;
// Knobs: ~110 local px for a full sweep feels right at 1.5-1.7x scale.
constexpr float kKnobThrow = 110.f;

constexpr int   kLongPressMs = 450;   // hold this long without moving => MIDI learn
constexpr float kLongPressSlop = 8.f; // local px the finger may wander

// Mouse listener that turns a press-and-hold on a control into a callback.
// Replaces the desktop right-click gesture (MIDI learn). Attach to a parent
// with addMouseListener(&lp, true) so it sees events from all children.
class LongPress : public juce::MouseListener, private juce::Timer
{
public:
  // Return true if the component was handled (nothing else happens either way;
  // the control keeps its normal drag behaviour).
  std::function<bool(juce::Component*)> onLongPress;

  void mouseDown(const juce::MouseEvent& e) override
  {
    // Only the first finger can start a long press.
    if (e.source.getIndex() != 0) return;
    mTarget = e.eventComponent;
    startTimer(kLongPressMs);
  }
  void mouseDrag(const juce::MouseEvent& e) override
  {
    if (e.source.getIndex() == 0 && e.getDistanceFromDragStart() > (int) kLongPressSlop)
      stopTimer();
  }
  void mouseUp(const juce::MouseEvent& e) override
  {
    if (e.source.getIndex() == 0) { stopTimer(); mTarget = nullptr; }
  }

private:
  void timerCallback() override
  {
    stopTimer();
    if (mTarget != nullptr && onLongPress) onLongPress(mTarget.getComponent());
  }
  juce::Component::SafePointer<juce::Component> mTarget;
};

// The Android app host (android/Main.cpp) sets this so the settings menu can open
// its Audio / MIDI / latency panel. Empty on desktop builds (no menu entry then).
inline std::function<void()>& audioMidiHook()
{
  static std::function<void()> f;
  return f;
}

// Pan / pinch-zoom listener. Attach to the editor with addMouseListener(&pz, true).
// Only gestures that START on "background" (no control underneath) are used, so
// moving faders/knobs with other fingers is never affected:
//   * one finger drag on background  -> onPan
//   * two fingers on background      -> onPinch (+ pan with the midpoint)
//   * double tap on background       -> onDoubleTap
class PanZoom : public juce::MouseListener
{
public:
  std::function<bool(juce::Component*)> isBackground;
  std::function<juce::Point<float>(juce::Point<float> screenPos)> toLocal;
  std::function<void(juce::Point<float> deltaPx)> onPan;
  std::function<void(float factor, juce::Point<float> focalPx)> onPinch;
  std::function<void(juce::Point<float> focalPx)> onDoubleTap;

  void mouseDown(const juce::MouseEvent& e) override
  {
    if (!isBackground || !isBackground(e.eventComponent)) return;
    mPts[e.source.getIndex()] = local(e);
  }
  void mouseDrag(const juce::MouseEvent& e) override
  {
    auto it = mPts.find(e.source.getIndex());
    if (it == mPts.end()) return;
    auto now = local(e);
    if (mPts.size() == 1)
    {
      if (onPan) onPan(now - it->second);
      it->second = now;
    }
    else if (mPts.size() >= 2)
    {
      auto a = mPts.begin(), b = std::next(a);
      auto oldA = a->second, oldB = b->second;
      it->second = now;
      auto newA = a->second, newB = b->second;
      float d0 = oldA.getDistanceFrom(oldB), d1 = newA.getDistanceFrom(newB);
      auto midOld = (oldA + oldB) * 0.5f, midNew = (newA + newB) * 0.5f;
      if (d0 > 20.f && onPinch) onPinch(d1 / d0, midNew);
      if (onPan) onPan(midNew - midOld);
    }
  }
  void mouseUp(const juce::MouseEvent& e) override { mPts.erase(e.source.getIndex()); }
  void mouseDoubleClick(const juce::MouseEvent& e) override
  {
    if (isBackground && isBackground(e.eventComponent) && onDoubleTap) onDoubleTap(local(e));
  }

private:
  juce::Point<float> local(const juce::MouseEvent& e) const
  {
    auto p = e.source.getScreenPosition();
    return toLocal ? toLocal(p) : p;
  }
  std::map<int, juce::Point<float>> mPts;
};

} // namespace kr106touch
