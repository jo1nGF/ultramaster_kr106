#pragma once

// MIDI learn bar (touch UI). Lives in its own strip at the bottom of the window, so it never
// covers a control, a label or a parameter value. While a control is waiting for a CC it shows
// what to do; afterwards it shows the assigned CC. Two buttons are always there:
//   Remove CC   - the control answers to no CC at all (not even its factory one)
//   Default CC  - forget the user's CC: the factory CC of that control (if it has one) applies again

#include <juce_gui_basics/juce_gui_basics.h>
#include "KR106TouchKeys.h"
#include "../PluginProcessor.h"

class KR106LearnBar : public juce::Component
{
public:
  explicit KR106LearnBar(KR106AudioProcessor& p)
    : mProc(p),
      mRemove("Remove CC", [this]() { onRemove(); }, juce::Colour(0xff6a1818)),
      mDefault("Default CC", [this]() { onDefault(); }),
      mClose("Cancel", [this]() { onClose(); })
  {
    addAndMakeVisible(mRemove);
    addAndMakeVisible(mDefault);
    addAndMakeVisible(mClose);
    setVisible(false);
  }

  void resized() override
  {
    auto r = getLocalBounds().reduced(3);
    mClose.setBounds(r.removeFromRight(72));
    r.removeFromRight(4);
    mDefault.setBounds(r.removeFromRight(100));
    r.removeFromRight(4);
    mRemove.setBounds(r.removeFromRight(100));
  }

  // Call from the editor timer (and right after a long press starts the learn)
  void tick()
  {
    const int lp = mProc.mMidiLearnParam.load(std::memory_order_relaxed);
    if (lp >= 0)
    {
      if (mState != Waiting || mParamIdx != lp) { mParamIdx = lp; setState(Waiting); }
    }
    else if (mState == Waiting)
      setState(mArmed ? Armed : Hidden);
    else if ((mState == Assigned || mState == Removed || mState == Defaulted) && juce::Time::getMillisecondCounter() > mDeadline)
      setState(mArmed ? Armed : Hidden);
    else if (mArmed && mState == Hidden)
      setState(Armed);
    else if (!mArmed && mState == Armed)
      setState(Hidden);
  }

  // The CC button (tap mode) is on: say what to do, with a way out
  void setArmed(bool armed) { mArmed = armed; tick(); }
  std::function<void()> onExit;

  // A CC was captured for the control that was waiting
  void showAssigned(int cc)
  {
    mAssignedCC = cc;
    mDeadline = juce::Time::getMillisecondCounter() + 6000;
    setState(Assigned);
  }

  void paint(juce::Graphics& g) override
  {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff000000));
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(mState == Waiting ? juce::Colour(0xff00dc00) : juce::Colour(0xff808080));
    g.drawRoundedRectangle(r, 4.f, 1.f);

    const auto text = message();
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(13.f));
    auto area = getLocalBounds().withTrimmedLeft(10).withTrimmedRight(286);
    g.drawFittedText(text, area, juce::Justification::centredLeft, 1, 0.75f);
  }

private:
  enum State { Hidden, Waiting, Assigned, Removed, Defaulted, Armed };

  juce::String paramName() const
  {
    auto* prm = (mParamIdx >= 0 && mParamIdx < kNumParams) ? mProc.getParam(mParamIdx) : nullptr;
    return prm != nullptr ? prm->getName(40) : juce::String("control");
  }

  juce::String message() const
  {
    const int user = mProc.hasUserCC(mParamIdx) ? mProc.getCCForParam(mParamIdx) : -1;
    const int fact = mProc.getDefaultCCForParam(mParamIdx);
    const bool off = mProc.isMidiCCDisabled(mParamIdx);
    switch (mState)
    {
      case Waiting:
        return "MIDI learn: " + paramName() + " - move a control on your keyboard"
               + (user >= 0 ? "   (now CC " + juce::String(user) + ")"
                  : off     ? juce::String("   (no CC)")
                  : fact >= 0 ? "   (default CC " + juce::String(fact) + ")" : juce::String("   (no CC)"));
      case Armed:
        return "MIDI learn: tap a control to map it, then move a control on your keyboard";
      case Assigned:
        return paramName() + " = CC " + juce::String(mAssignedCC);
      case Removed:
        return paramName() + ": CC removed - it answers to no CC";
      case Defaulted:
        return paramName() + (fact >= 0 ? ": back to the default CC " + juce::String(fact) : juce::String(": default - no CC"));
      default: return {};
    }
  }

  void refreshButtons()
  {
    mRemove.setVisible(mState != Armed);
    mDefault.setVisible(mState != Armed);
    mClose.setText(mState == Waiting ? "Cancel" : mState == Armed ? "Exit" : "OK");
  }

  void setState(State s)
  {
    mState = s;
    refreshButtons();
    setVisible(s != Hidden);
    if (s != Hidden) toFront(false);
    repaint();
  }

  void onRemove()
  {
    mProc.disableMidiCC(mParamIdx);       // also ends the learn if it is waiting
    mDeadline = juce::Time::getMillisecondCounter() + 4000;
    setState(Removed);
  }

  void onDefault()
  {
    mProc.clearMidiLearn(mParamIdx);      // also ends the learn if it is waiting
    mDeadline = juce::Time::getMillisecondCounter() + 4000;
    setState(Defaulted);
  }

  void onClose()
  {
    if (mState == Armed) { if (onExit) onExit(); return; }                 // the CC mode ends
    if (mState == Waiting) mProc.cancelMidiLearn();
    setState(mArmed ? Armed : Hidden);
  }

  KR106AudioProcessor& mProc;
  KR106TouchTextButton mRemove, mDefault, mClose;
  State mState = Hidden;
  int mParamIdx = -1, mAssignedCC = -1;
  bool mArmed = false;
  juce::uint32 mDeadline = 0;
};


// MIDI learn by tapping: with the "CC" button on, this covers the page; a tap on a control starts the learn
// for it (the control does not move), then the CC you move on your keyboard is assigned. The tab bar stays usable.
class KR106LearnTapOverlay : public juce::Component, private juce::Timer
{
public:
  std::function<std::vector<juce::Rectangle<int>>()> outlines;     // the learnable controls that are showing
  std::function<void(juce::Point<int>)> onTap;
  std::function<bool(juce::Point<int>)> passThrough;               // true: let the touch reach what is underneath

  KR106LearnTapOverlay() { startTimerHz(6); }

  bool hitTest(int x, int y) override { return !(passThrough && passThrough({ x, y })); }

  void paint(juce::Graphics& g) override
  {
    g.fillAll(juce::Colour(0x28000000));
    if (outlines)
      for (auto& r : outlines())
      {
        g.setColour(juce::Colour(0xff00dc00).withAlpha(0.85f));
        g.drawRoundedRectangle(r.toFloat().expanded(3.f), 4.f, 1.5f);
      }
  }

  void mouseUp(const juce::MouseEvent& e) override { if (onTap) onTap(e.getPosition()); }

private:
  void timerCallback() override { if (isShowing()) repaint(); }
};
