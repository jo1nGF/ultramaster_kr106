#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KR106Tooltip.h"
#include "../PluginProcessor.h"

// Draw a 3D bevel button (pixel-perfect, no AA).
// Types: 0=Cream, 1=Yellow, 2=Orange.
// When pressed: highlight and shadow swap.
static inline void DrawKR106Button(juce::Graphics& g, float l, float t, float r, float b, int type, bool pressed)
{
    struct ColorTriplet { juce::Colour highlight, face, shadow; };

    static const ColorTriplet colors[3] = {
        { juce::Colour(253, 247, 203), juce::Colour(220, 220, 178), juce::Colour(178, 179, 144) }, // Cream
        { juce::Colour(253, 247, 203), juce::Colour(228, 229, 88),  juce::Colour(180, 179, 69)  }, // Yellow
        { juce::Colour(253, 247, 203), juce::Colour(240, 173, 56),  juce::Colour(180, 126, 41)  }, // Orange
    };

    const auto& c = colors[type];
    const auto hi = pressed ? c.shadow : c.highlight;
    const auto lo = pressed ? c.highlight : c.shadow;
    const auto black = juce::Colour(0, 0, 0);

    // Face fill
    g.setColour(c.face);
    g.fillRect(l + 2, t + 2, r - l - 4, b - t - 4);

    // 3D bevel — top/left = highlight, bottom/right = shadow
    g.setColour(hi);
    g.fillRect(l + 1, t + 1, r - l - 2, 1.f);   // top highlight
    g.fillRect(l + 1, t + 1, 1.f, b - t - 2);    // left highlight
    g.setColour(lo);
    g.fillRect(l + 2, b - 2, r - l - 3, 1.f);    // bottom shadow
    g.fillRect(r - 2, t + 2, 1.f, b - t - 3);    // right shadow

    // 1px black border
    g.setColour(black);
    g.fillRect(l, t, r - l, 1.f);                 // top
    g.fillRect(l, b - 1, r - l, 1.f);             // bottom
    g.fillRect(l, t, 1.f, b - t);                 // left
    g.fillRect(r - 1, t, 1.f, b - t);             // right
}

// ============================================================================
// KR106ButtonLED — Button with LED indicator above it.
// Total size: 17x28 (LED 9x9 at top, button 17x19 below).
// LED image is @2x, 18x36, 2 frames stacked vertically (frame 0=off, frame 1=on).
// LED is on when param value > 0.5 AND power param value > 0.5.
// ============================================================================
class KR106ButtonLED : public juce::Component
{
public:
    KR106ButtonLED(juce::RangedAudioParameter* param, int buttonType,
