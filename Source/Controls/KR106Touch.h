#pragma once

// Touch-screen support (phones / tablets). Enabled with -DKR106_TOUCH=1
// (CMake option KR106_TOUCH). On desktop builds everything here is inert.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

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

} // namespace kr106touch
