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
    KR106Switch(juce::RangedAudioParameter* param, const juce::Image& spriteSheet,
                int numPositions, KR106Tooltip* tip = nullptr)
        : mParam(param)
        , mSpriteSheet(spriteSheet)
        , mNumPositions(numPositions)
        , mTooltip(tip)
    {
    }

    void setMidiLearn(KR106AudioProcessor* proc, int paramIdx)
    { mProcessor = proc; mParamIdx = paramIdx; }

    void paint(juce::Graphics& g) override
    {
        float val = mParam ? mParam->getValue() : 0.f;
        int pos = (int)std::round(val * (mNumPositions - 1));
        pos = juce::jlimit(0, mNumPositions - 1, pos);

        // Map position to sprite frame: 2-way uses frames 0,2; 3-way uses 0,1,2
        int spriteIdx = (mNumPositions == 2) ? (pos == 0 ? 0 : 2) : pos;

        int frameW2x = mSpriteSheet.getWidth();
        int frameH2x = mSpriteSheet.getHeight() / 3; // always 3 frames in sheet
        float frameW = frameW2x / 2.f;
        float frameH = frameH2x / 2.f;

        g.drawImage(mSpriteSheet,
                    0.f, 0.f, frameW, frameH,
                    0, spriteIdx * frameH2x, frameW2x, frameH2x);

        paintMidiLearnBorder(g);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (mTooltip)
        {
            updateCCLine();
            mTooltip->show(mParam, this);
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (mTooltip) mTooltip->hide();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && mProcessor && mParamIdx >= 0)
        {
            mProcessor->startMidiLearn(mParamIdx);
            updateCCLine();
            if (mTooltip) mTooltip->show(mParam, this);
            repaint();
            return;
        }
        if (e.mods.isPopupMenu()) return;
        if (mProcessor && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) >= 0)
        {
            mProcessor->cancelMidiLearn();
            if (mTooltip) mTooltip->hide();
            repaint();
        }
        mDragStartY = e.position.y;
        mDragStartIdx = currentIdx();
        mDragged = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!mParam || mNumPositions <= 1) return;

        float totalDrag = e.position.y - mDragStartY;
