#pragma once

// MIDI learn bar (touch UI). Lives in its own strip at the bottom of the window, so it never
// covers a control, a label or a parameter value. While a control is waiting for a CC it shows
// what to do; afterwards it shows the assigned CC. "Remove CC" deletes the CC the user assigned,
// so the factory CC of that control (if it has one) applies again.

#include <juce_gui_basics/juce_gui_basics.h>
#include "KR106TouchKeys.h"
#include "../PluginProcessor.h"

class KR106LearnBar : public juce::Component
{
public:
  explicit KR106LearnBar(KR106AudioProcessor& p)
    : mProc(p),
      mRemove("Remove CC", [this]() { onRemove(); }, juce::Colour(0xff6a1818)),
      mClose("Cancel", [this]() { onClose(); })
  {
    addAndMakeVisible(mRemove);
    addAndMakeVisible(mClose);
    setVisible(false);
  }

  void resized() override
  {
    auto r = getLocalBounds().reduced(3);
    mClose.setBounds(r.removeFromRight(78));
    r.removeFromRight(4);
    mRemove.setBounds(r.removeFromRight(104));
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
      setState(Hidden);
    else if ((mState == Assigned || mState == Removed) && juce::Time::getMillisecondCounter() > mDeadline)
      setState(Hidden);
  }

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
    auto area = getLocalBounds().withTrimmedLeft(10).withTrimmedRight(mRemove.isVisible() ? 192 : 90);
    g.drawFittedText(text, area, juce::Justification::centredLeft, 1, 0.75f);
  }

private:
  enum State { Hidden, Waiting, Assigned, Removed };

  juce::String paramName() const
  {
    auto* prm = (mParamIdx >= 0 && mParamIdx < kNumParams) ? mProc.getParam(mParamIdx) : nullptr;
    return prm != nullptr ? prm->getName(40) : juce::String("control");
  }

  juce::String message() const
  {
    const int user = mProc.hasUserCC(mParamIdx) ? mProc.getCCForParam(mParamIdx) : -1;
    const int fact = mProc.getDefaultCCForParam(mParamIdx);
    switch (mState)
    {
      case Waiting:
        return "MIDI learn: " + paramName() + " - move a control on your keyboard"
               + (user >= 0 ? "   (now CC " + juce::String(user) + ")"
                            : fact >= 0 ? "   (factory CC " + juce::String(fact) + ")" : juce::String());
      case Assigned:
        return paramName() + " = CC " + juce::String(mAssignedCC);
      case Removed:
        return paramName() + ": CC removed - " + (fact >= 0 ? "back to factory CC " + juce::String(fact) : juce::String("no CC assigned"));
      default: return {};
    }
  }

  void refreshButtons()
  {
    mRemove.setVisible(mState != Removed && mProc.hasUserCC(mParamIdx));
    mClose.setText(mState == Waiting ? "Cancel" : "OK");
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
    mProc.clearMidiLearn(mParamIdx);      // also ends the learn if it is waiting
    mDeadline = juce::Time::getMillisecondCounter() + 4000;
    setState(Removed);
  }

  void onClose()
  {
    if (mState == Waiting) mProc.cancelMidiLearn();
    setState(Hidden);
  }

  KR106AudioProcessor& mProc;
  KR106TouchTextButton mRemove, mClose;
  State mState = Hidden;
  int mParamIdx = -1, mAssignedCC = -1;
  juce::uint32 mDeadline = 0;
};
