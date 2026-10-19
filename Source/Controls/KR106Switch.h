#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "KR106Touch.h"

#include "KR106Tooltip.h"
#include "PluginProcessor.h"

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

        if constexpr (kr106touch::kEnabled)
        {
            // Vector toggle (touch UI): slot + metal thumb that slides to the position
            auto r = getLocalBounds().toFloat();
            auto slot = r.reduced(0.6f);
            g.setColour(juce::Colour(0xff060606));
            g.fillRoundedRectangle(slot, slot.getWidth() * 0.45f);
            g.setColour(juce::Colour(0xff7a7a7a));
            g.drawRoundedRectangle(slot, slot.getWidth() * 0.45f, 0.8f);
            const float t = mNumPositions > 1 ? (float) pos / (float)(mNumPositions - 1) : 0.f;
            const float th = 9.5f, tw = r.getWidth() - 2.2f;
            const float ty = 1.1f + t * (r.getHeight() - 2.2f - th);
            juce::Rectangle<float> thumb(1.1f, ty, tw, th);
            juce::ColourGradient grad(juce::Colour(0xfff2f2f2), thumb.getX(), thumb.getY(),
                                      juce::Colour(0xff8c8c8c), thumb.getX(), thumb.getBottom(), false);
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.fillRoundedRectangle(thumb.translated(0.f, 0.9f), 2.f);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(thumb, 2.f);
            g.setColour(juce::Colour(0xff111111));
            g.drawRoundedRectangle(thumb, 2.f, 0.8f);
            g.setColour(juce::Colour(0xff2a2a2a));
            g.fillRect(thumb.getX() + 1.2f, thumb.getCentreY() - 0.3f, thumb.getWidth() - 2.4f, 0.7f);
            paintMidiLearnBorder(g);
            return;
        }

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
        if (std::abs(totalDrag) > 3.f) mDragged = true;

        int frameH2x = mSpriteSheet.getHeight() / 3;
        float frameH = frameH2x / 2.f;

        int steps = (int)(totalDrag / (frameH * 0.5f));
        int newIdx = mDragStartIdx + steps;
        newIdx = juce::jlimit(0, mNumPositions - 1, newIdx);

        float newVal = (mNumPositions > 1) ? (float)newIdx / (float)(mNumPositions - 1) : 0.f;
        mParam->beginChangeGesture();
        mParam->setValueNotifyingHost(newVal);
        mParam->endChangeGesture();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override
    {
        if (!mDragged && mParam && mNumPositions > 1)
        {
            int idx = (currentIdx() + 1) % mNumPositions;
            float newVal = (float)idx / (float)(mNumPositions - 1);
            mParam->beginChangeGesture();
            mParam->setValueNotifyingHost(newVal);
            mParam->endChangeGesture();
            repaint();
        }
    }

private:
    int currentIdx() const
    {
        float val = mParam ? mParam->getValue() : 0.f;
        return juce::jlimit(0, mNumPositions - 1, (int)std::round(val * (mNumPositions - 1)));
    }

    void paintMidiLearnBorder(juce::Graphics& g)
    {
        if (mProcessor && mParamIdx >= 0
            && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
        {
            g.setColour(juce::Colour(0, 255, 0));
            g.drawRect(getLocalBounds(), 1);
        }
    }

    void updateCCLine()
    {
        if (!mTooltip) return;
        if (!mProcessor || mParamIdx < 0) { mTooltip->setLine2({}); return; }
        if (mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
        {
            int cc = mProcessor->getCCForParam(mParamIdx);
            mTooltip->setLine2(cc >= 0 ? "MIDI LEARN (CC " + juce::String(cc) + ")  Del: remove  Bksp: default" : "MIDI LEARN");
            return;
        }
        int cc = mProcessor->getCCForParam(mParamIdx);
        mTooltip->setLine2(cc >= 0 ? "CC " + juce::String(cc) : "CC ??");
    }

    juce::RangedAudioParameter* mParam = nullptr;
    KR106AudioProcessor* mProcessor = nullptr;
    int mParamIdx = -1;
    KR106Tooltip* mTooltip = nullptr;
    juce::Image mSpriteSheet;
    int mNumPositions = 2;
    float mDragStartY = 0.f;
    int mDragStartIdx = 0;
    bool mDragged = false;
};

// ============================================================================
// KR106HorizontalSwitch — Horizontal bitmap-based multi-position switch.
// Always uses the 3-way horizontal sprite sheet (3 frames stacked vertically, @2x).
// For 2-way params: displays frames 0 and 2 (skipping middle).
// Drag is horizontal instead of vertical.
// ============================================================================
class KR106HorizontalSwitch : public juce::Component
{
public:
    KR106HorizontalSwitch(juce::RangedAudioParameter* param, const juce::Image& spriteSheet,
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

        if constexpr (kr106touch::kEnabled)
        {
            // Vector horizontal toggle (touch UI)
            auto r = getLocalBounds().toFloat();
            auto slot = r.reduced(0.6f);
            g.setColour(juce::Colour(0xff060606));
            g.fillRoundedRectangle(slot, slot.getHeight() * 0.45f);
            g.setColour(juce::Colour(0xff7a7a7a));
            g.drawRoundedRectangle(slot, slot.getHeight() * 0.45f, 0.8f);
            const float t = mNumPositions > 1 ? (float) pos / (float)(mNumPositions - 1) : 0.f;
            const float tw = 9.5f, th = r.getHeight() - 2.2f;
            const float tx = 1.1f + t * (r.getWidth() - 2.2f - tw);
            juce::Rectangle<float> thumb(tx, 1.1f, tw, th);
            juce::ColourGradient grad(juce::Colour(0xfff2f2f2), thumb.getX(), thumb.getY(),
                                      juce::Colour(0xff8c8c8c), thumb.getX(), thumb.getBottom(), false);
            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.fillRoundedRectangle(thumb.translated(0.f, 0.8f), 2.f);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(thumb, 2.f);
            g.setColour(juce::Colour(0xff111111));
            g.drawRoundedRectangle(thumb, 2.f, 0.8f);
            g.setColour(juce::Colour(0xff2a2a2a));
            g.fillRect(thumb.getCentreX() - 0.3f, thumb.getY() + 1.f, 0.7f, thumb.getHeight() - 2.f);
            paintMidiLearnBorder(g);
            return;
        }

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
        mDragStartX = e.position.x;
        mDragStartIdx = currentIdx();
        mDragged = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!mParam || mNumPositions <= 1) return;

        float totalDrag = e.position.x - mDragStartX;
        if (std::abs(totalDrag) > 3.f) mDragged = true;

        float frameW = mSpriteSheet.getWidth() / 2.f;

        int steps = (int)(totalDrag / (frameW * 0.5f));
        int newIdx = mDragStartIdx + steps;
        newIdx = juce::jlimit(0, mNumPositions - 1, newIdx);

        float newVal = (mNumPositions > 1) ? (float)newIdx / (float)(mNumPositions - 1) : 0.f;
        mParam->beginChangeGesture();
        mParam->setValueNotifyingHost(newVal);
        mParam->endChangeGesture();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override
    {
        if (!mDragged && mParam && mNumPositions > 1)
        {
            int idx = (currentIdx() + 1) % mNumPositions;
            float newVal = (float)idx / (float)(mNumPositions - 1);
            mParam->beginChangeGesture();
            mParam->setValueNotifyingHost(newVal);
            mParam->endChangeGesture();
            repaint();
        }
    }

private:
    int currentIdx() const
    {
        float val = mParam ? mParam->getValue() : 0.f;
        return juce::jlimit(0, mNumPositions - 1, (int)std::round(val * (mNumPositions - 1)));
    }

    void paintMidiLearnBorder(juce::Graphics& g)
    {
        if (mProcessor && mParamIdx >= 0
            && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
        {
            g.setColour(juce::Colour(0, 255, 0));
            g.drawRect(getLocalBounds(), 1);
        }
    }

    void updateCCLine()
    {
        if (!mTooltip) return;
        if (!mProcessor || mParamIdx < 0) { mTooltip->setLine2({}); return; }
        if (mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
        {
            int cc = mProcessor->getCCForParam(mParamIdx);
            mTooltip->setLine2(cc >= 0 ? "MIDI LEARN (CC " + juce::String(cc) + ")  Del: remove  Bksp: default" : "MIDI LEARN");
            return;
        }
        int cc = mProcessor->getCCForParam(mParamIdx);
        mTooltip->setLine2(cc >= 0 ? "CC " + juce::String(cc) : "CC ??");
    }

    juce::RangedAudioParameter* mParam = nullptr;
    KR106AudioProcessor* mProcessor = nullptr;
    int mParamIdx = -1;
    KR106Tooltip* mTooltip = nullptr;
    juce::Image mSpriteSheet;
    int mNumPositions = 2;
    float mDragStartX = 0.f;
    int mDragStartIdx = 0;
    bool mDragged = false;
};
