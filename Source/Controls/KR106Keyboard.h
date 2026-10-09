#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KR106Tooltip.h"
#include "KR106Touch.h"
#include "../PluginProcessor.h"

// ============================================================================
// KR106Knob — Bitmap knob with manhattan distance drag.
// Sprite sheet is @2x, N frames in a horizontal strip.
// Frame count is auto-detected: numFrames = imageWidth / imageHeight.
// Both vertical and horizontal mouse movement contribute to the value.
// Up and right increase; down and left decrease.
// ============================================================================
class KR106Knob : public juce::Component
{
public:
    KR106Knob(juce::RangedAudioParameter* param, const juce::Image& spriteSheet,
              KR106Tooltip* tip = nullptr, int numFrames = 0)
        : mParam(param)
        , mSpriteSheet(spriteSheet)
        , mTooltip(tip)
        , mNumFrames(numFrames > 0 ? numFrames
                     : (spriteSheet.getHeight() > 0 ? spriteSheet.getWidth() / spriteSheet.getHeight() : 32))
    {
    }

    void setBackgroundImage(const juce::Image& img) { mBgImage = img; }

    void setMidiLearn(KR106AudioProcessor* proc, int paramIdx)
    { mProcessor = proc; mParamIdx = paramIdx; }

    void setSnapCenter(bool snap) { mSnapCenter = snap; }
    void setGearing(float g) { mGearing = g; }

    // Optional callback fired on mouse-up at the end of a drag (after the
    // gesture is finalized). Use for side-effects that should happen once
    // per gesture rather than on every drag-pixel value change.
    std::function<void()> onDragEnd;

    void paint(juce::Graphics& g) override
    {
        float val = mParam ? mParam->getValue() : 0.f;
        int maxIdx = mNumFrames - 1;
        int idx = (int)std::round(val * (float)maxIdx);
        idx = juce::jlimit(0, maxIdx, idx);

        int frameW2x = mSpriteSheet.getWidth() / mNumFrames;
        int h2x = mSpriteSheet.getHeight();
        float frameW = frameW2x / 2.f;
        float frameH = h2x / 2.f;

        float w = (float)getWidth();
        float h = (float)getHeight();

        if (mBgImage.isValid())
        {
            g.drawImage(mBgImage,
                        0.f, 0.f, w, h,
                        0, 0, mBgImage.getWidth(), mBgImage.getHeight());
        }

        // Centre sprite within bounds
        float ox = (w - frameW) * 0.5f;
        float oy = (h - frameH) * 0.5f;
        g.drawImage(mSpriteSheet,
                    ox, oy, frameW, frameH,                        // dest: 1x
                    idx * frameW2x, 0, frameW2x, h2x);            // src: 2x

        // Green border when in MIDI learn mode
        if (mProcessor && mParamIdx >= 0
            && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
        {
            g.setColour(juce::Colour(0, 255, 0));
            g.drawRect(getLocalBounds(), 1);
        }
    }

