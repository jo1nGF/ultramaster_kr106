#pragma once

// The "Seq" page: holds the two step sequencers and a small switch to alternate between them.
//   VARIGATE   - Malekko Varigate 8+ style (8 gate tracks + 2 CV tracks, 16 steps)
//   METROPOLIX - Intellijel Metropolix style (2 note tracks, 8 stages, MOD lanes)
// Only the selected one plays; switching stops the other and its notes. The choice is saved with the state.

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "../PluginProcessor.h"
#include "KR106VarigatePanel.h"
#include "KR106MetropolixPanel.h"

class KR106SeqPage : public juce::Component, private juce::Timer
{
public:
  explicit KR106SeqPage(KR106AudioProcessor& p) : mProc(p), mVarigate(p), mMetropolix(p), mSwitch(p)
  {
    addAndMakeVisible(mVarigate);
    addAndMakeVisible(mMetropolix);
    addAndMakeVisible(mSwitch);                  // last: on top of the panels
    mSwitch.onChange = [this]() { refresh(); };
    startTimerHz(10);
    refresh();
  }

  void resized() override
  {
    const auto b = getLocalBounds();
    mVarigate.setBounds(b);
    mMetropolix.setBounds(b);
    mSwitch.setBounds(6, b.getHeight() - 22, 118, 20);
  }

private:
  class ModeSwitch : public juce::Component
  {
  public:
    explicit ModeSwitch(KR106AudioProcessor& p) : mProc(p) {}
    std::function<void()> onChange;

    void paint(juce::Graphics& g) override
    {
      const int mode = mProc.getSeqMode();
      const auto b = getLocalBounds().toFloat().reduced(0.5f);
      g.setColour(juce::Colour(0xff0e0e10));
      g.fillRoundedRectangle(b, 4.f);
      const float half = b.getWidth() / 2.f;
      const juce::Rectangle<float> seg(b.getX() + (mode == 0 ? 0.f : half), b.getY(), half, b.getHeight());
      g.setColour(juce::Colour(0xffe9eef7));
      g.fillRoundedRectangle(seg.reduced(1.5f), 3.f);
      g.setColour(juce::Colour(0xff7a7c82));
      g.drawRoundedRectangle(b, 4.f, 1.f);
      g.setFont(juce::FontOptions(9.f, juce::Font::bold));
      g.setColour(mode == 0 ? juce::Colour(0xff101010) : juce::Colour(0xffc4c6cc));
      g.drawText("VARIGATE", 0, 0, getWidth() / 2, getHeight(), juce::Justification::centred);
      g.setColour(mode == 1 ? juce::Colour(0xff101010) : juce::Colour(0xffc4c6cc));
      g.drawText("METROPOLIX", getWidth() / 2, 0, getWidth() / 2, getHeight(), juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
      mProc.setSeqMode(e.getPosition().x < getWidth() / 2 ? 0 : 1);
      repaint();
      if (onChange) onChange();
    }

  private:
    KR106AudioProcessor& mProc;
  };

  void timerCallback() override { if (mProc.getSeqMode() != mShown) refresh(); }

  void refresh()
  {
    mShown = mProc.getSeqMode();
    mVarigate.setVisible(mShown == 0);
    mMetropolix.setVisible(mShown == 1);
    mSwitch.repaint();
  }

  KR106AudioProcessor& mProc;
  KR106VarigatePanel mVarigate;
  KR106MetropolixPanel mMetropolix;
  ModeSwitch mSwitch;
  int mShown = -1;
};
