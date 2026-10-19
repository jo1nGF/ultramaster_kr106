#pragma once

// Envelope display for the Mod page: the shape of the modulation envelope (drawn from the controls,
// including whatever the modulation matrix is adding to them right now) and a red dot that follows
// the real envelope while a note plays. Green on black like the scope.

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include "../PluginProcessor.h"

class KR106ModEnvDisplay : public juce::Component, private juce::Timer
{
public:
  explicit KR106ModEnvDisplay(KR106AudioProcessor& p) : mProc(p) { startTimerHz(30); }

  void paint(juce::Graphics& g) override
  {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(bounds, 3.f);
    g.setColour(juce::Colour(0xff0a3a0a));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.f, 1.f);

    const float att = mProc.modDisplayValue(kModEnvAtt), dec = mProc.modDisplayValue(kModEnvDec),
                sus = mProc.modDisplayValue(kModEnvSStart), stm = mProc.modDisplayValue(kModEnvSTime),
                rel = mProc.modDisplayValue(kModEnvRel), amp = mProc.modDisplayValue(kModEnvAmp);
    float tA, tD, tS, tR;
    kr106::ModEnv::Times(att, dec, stm, rel, tA, tD, tS, tR);
    // how long the key is held in the drawing: the sustain slope, but not longer than the rest needs
    const float hold = std::min(tS, 0.3f * (tA + tD + tR) + 0.1f);
    const float tGate = tA + tD + hold;
    const float total = tGate + tR;

    const float x0 = 4.f, w = bounds.getWidth() - 8.f, yTop = 4.f, yBot = bounds.getHeight() - 4.f;
    auto X = [&](float t) { return x0 + w * (t / total); };
    auto Y = [&](float v) { return yBot - (yBot - yTop) * std::max(0.f, std::min(1.f, v)); };

    // stage boundaries
    g.setColour(juce::Colour(0xff0d4d0d));
    const float marks[3] = { tA, tA + tD, tGate };
    for (float m : marks) g.drawVerticalLine(juce::roundToInt(X(m)), yTop, yBot);

    // curve
    juce::Path path;
    const int steps = juce::jmax(16, juce::roundToInt(w));
    for (int i = 0; i <= steps; i++)
    {
      const float t = total * static_cast<float>(i) / static_cast<float>(steps);
      const float v = kr106::ModEnv::CurveAt(t, tGate, tA, tD, tS, tR, sus) * amp;
      if (i == 0) path.startNewSubPath(X(t), Y(v)); else path.lineTo(X(t), Y(v));
    }
    g.setColour(juce::Colour(0xff00ff00));
    g.strokePath(path, juce::PathStrokeType(2.f));

    // the real envelope
    const int stage = mProc.mModEnvStage.load(std::memory_order_relaxed);
    if (stage != 0)
    {
      const float ts = mProc.mModEnvT.load(std::memory_order_relaxed);
      float t = 0.f;
      switch (stage)
      {
        case 1: t = std::min(ts, tA); break;
        case 2: t = tA + std::min(ts, tD); break;
        case 3: t = tA + tD + std::min(ts, hold); break;
        default: t = tGate + std::min(ts, tR); break;
      }
      const float v = mProc.mModEnvLevel.load(std::memory_order_relaxed) * amp;
      g.setColour(juce::Colour(0xffff2020));
      g.fillEllipse(X(t) - 4.f, Y(v) - 4.f, 8.f, 8.f);
    }
  }

private:
  void timerCallback() override { if (isShowing()) repaint(); }
  KR106AudioProcessor& mProc;
};
