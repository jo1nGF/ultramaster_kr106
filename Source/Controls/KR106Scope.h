#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"

// ============================================================================
// KR106Keyboard — pixel-perfect 61-key keyboard (C2-C7)
// 792x114 pixels: 5px strip for transpose chevron + 109px keys
// Port of KR106KeyboardControl from iPlug2
// ============================================================================
class KR106Keyboard : public juce::Component
{
public:
    static constexpr int kMinNote = 36;  // C2
    static constexpr int kNumKeys = 61;

    KR106Keyboard(KR106AudioProcessor* processor, const juce::Image& chevronImage)
        : mProcessor(processor)
        , mChevronImage(chevronImage)
    {
    }

    void paint(juce::Graphics& g) override
    {
        float ox = 0.f;
        float strip = 0.f;  // 5px strip at top for transpose indicator
        float oy = 5.f;     // keys start here

        // Fill entire white-key area first to avoid sub-pixel gaps on Windows
        g.setColour(kKB_Light);
        g.fillRect(ox, oy, 792.f, 107.f);

        for (int oct = 0; oct < 5; oct++)
        {
            int key = oct * 12;
            float x = ox + oct * 154;
            KB_DrawLeft   (g, x,       oy, mKeys[key]);
            KB_DrawBlack  (g, x + 13,  oy, mKeys[key + 1]);
            KB_DrawMiddle1(g, x + 22,  oy, mKeys[key + 2]);
            KB_DrawBlack  (g, x + 40,  oy, mKeys[key + 3]);
            KB_DrawRight  (g, x + 44,  oy, mKeys[key + 4]);
            KB_DrawLeft   (g, x + 66,  oy, mKeys[key + 5]);
            KB_DrawBlack  (g, x + 79,  oy, mKeys[key + 6]);
            KB_DrawMiddle2(g, x + 88,  oy, mKeys[key + 7]);
            KB_DrawBlack  (g, x + 103, oy, mKeys[key + 8]);
            KB_DrawMiddle3(g, x + 110, oy, mKeys[key + 9]);
            KB_DrawBlack  (g, x + 128, oy, mKeys[key + 10]);
            KB_DrawRight  (g, x + 132, oy, mKeys[key + 11]);
        }
        KB_DrawLast(g, ox + 770, oy, mKeys[60]);

        // Transpose indicator — chevron bitmap in the 5px strip above keys
        if (mTransposeKey >= 0)
        {
            static const int xOffsets[12] = {0, 13, 22, 40, 44, 66, 79, 88, 103, 110, 128, 132};
            static const float kCX[12]    = {7.f, 7.f,12.f, 7.f,16.f, 7.f, 7.f,10.f, 7.f,13.f, 7.f,16.f};
            float kx = (mTransposeKey == 60) ? ox + 770
