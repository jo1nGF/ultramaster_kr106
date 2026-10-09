#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KR106Tooltip.h"
#include "../PluginProcessor.h"

// ============================================================================
// KR106Switch — Vertical bitmap-based multi-position switch.
// Always uses the 3-way sprite sheet (3 frames stacked vertically, @2x).
// For 2-way params: displays frames 0 and 2 (skipping middle).
// Click cycles through states; vertical drag snaps to nearest position.
// ============================================================================
class KR106Switch : public juce::Component
{
public:
