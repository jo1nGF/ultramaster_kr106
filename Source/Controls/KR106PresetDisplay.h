#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "KR106Touch.h"
#include "../PluginProcessor.h"
#include "KR106MenuSheet.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ============================================================================
// KR106Scope -- oscilloscope display (green waveform on black)
// Triggered by oscillator sync pulse; reads from processor's ring buffer
// Port of KR106ScopeControl from iPlug2
// ============================================================================
class KR106Scope : public juce::Component
{
public:
    static constexpr int RING_SIZE = 4096;

    // Shared scope drawing constants
    static constexpr float kTickW = 1.f;
    static constexpr float kTickH = 3.f;
    static constexpr float kGridW = 1.f;
    static constexpr int   kTickSpacing = 5;
    static inline juce::Colour cBlack()  { return juce::Colour(0, 0, 0); }
    static inline juce::Colour cGrid()   { return juce::Colour(0, 64, 0); }
    static inline juce::Colour cDim()    { return juce::Colour(0, 128, 0); }
    static inline juce::Colour cMid()    { return juce::Colour(0, 192, 0); }
    static inline juce::Colour cBright() { return juce::Colour(0, 255, 0); }

    // Center crosshairs with tick marks every kTickSpacing px
    void paintCrosshairs(juce::Graphics& g, int w, int h, juce::Colour /*dim*/)
    {
        float cx = std::round(w * 0.5f);
        float cy = std::round(h * 0.5f);

        // Center lines
        g.setColour(cGrid());
        g.fillRect(cx, 0.f, kGridW, static_cast<float>(h));
        g.fillRect(0.f, cy, static_cast<float>(w), kGridW);

        // Ticks on horizontal center line (radiating from center, shifted 1px down)
        for (int i = kTickSpacing; ; i += kTickSpacing)
        {
            float xr = cx + i, xl = cx - i;
            bool any = false;
            if (xr < w) { g.fillRect(xr, cy - kTickH * 0.5f + 0.5f, kTickW, kTickH); any = true; }
            if (xl >= 0) { g.fillRect(xl, cy - kTickH * 0.5f + 0.5f, kTickW, kTickH); any = true; }
            if (!any) break;
        }

        // Ticks on vertical center line (radiating from center, shifted 1px right)
        for (int i = kTickSpacing; ; i += kTickSpacing)
        {
            float yd = cy + i, yu = cy - i;
            bool any = false;
            if (yd < h) { g.fillRect(cx - kTickH * 0.5f + 0.5f, yd, kTickH, kTickW); any = true; }
            if (yu >= 0) { g.fillRect(cx - kTickH * 0.5f + 0.5f, yu, kTickH, kTickW); any = true; }
