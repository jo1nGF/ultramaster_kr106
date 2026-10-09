#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "KR106MenuSheet.h"

// ============================================================================
// KR106VarianceSheet — per-voice component variance editor.
// Grid showing 10 voices × 6 variance parameters with drag-to-edit.
// Black background, green LED font, same aesthetic as preset/menu sheets.
// ============================================================================
class KR106VarianceSheet : public juce::Component
{
public:
    static constexpr int kMaxVoices = 10;
    static constexpr int kNumParams = 5;
    // Layout: title, noise row (analog + chorus on one line), header, 10 voices
    static constexpr int kFirstVoiceRow = 3; // title + noise + header
    static constexpr int kTotalRows = kFirstVoiceRow + kMaxVoices;
    static constexpr int kLabelCol = 1;   // voice label column
    static constexpr int kTotalCols = kLabelCol + kNumParams;

    KR106VarianceSheet(KR106AudioProcessor* proc, juce::Typeface::Ptr typeface,
                       std::function<void()> onClose)
        : mProcessor(proc), mTypeface(typeface), mOnClose(std::move(onClose))
    {
        setWantsKeyboardFocus(true);
        readFromVoices();
        if (mProcessor)
        {
            mAnalogNoiseKnob = mulToKnob(mProcessor->mDSP.mNoiseFloorMul);
            mMainsNoiseKnob = mulToKnob(mProcessor->mDSP.mMainsMul);
            mClockNoiseKnob = mulToKnob(mProcessor->mDSP.mChorus.mClockMul);
            mBbdDriveKnob = mProcessor->mDSP.mChorus.mBbdDriveUser / 100.f;
            mDriftKnob = mProcessor->mDSP.mDriftAmount;
            // Note: chorus mAnalogMul is always kept in sync with mNoiseFloorMul
        }
    }

    // Cell sizes match the 8×16 preset sheet grid (940/8 = 117w, 224/16 = 14h)
    static constexpr int kColW = 117;
    static constexpr int kRowH = 14;

    int calcWidth() const { return kTotalCols * kColW; }
    int calcHeight() const { return kTotalRows * kRowH + 2; }

    void showAt(juce::Rectangle<int> bounds)
    {
        setBounds(bounds);
        grabKeyboardFocus();
    }

    void paint(juce::Graphics& g) override
    {
        using namespace KR106Theme;
        g.fillAll(bg());
        g.setFont(ledFont(mTypeface));

        int w = getWidth();
        int h = getHeight();
        int activeVoices = mProcessor ? mProcessor->mVoiceCount : 6;

        // Title row: 3 cols for title, 4 cols for buttons (right-aligned)
        int colW7 = w / kTotalCols;
        g.setColour(bright());
        g.drawSingleLineText("COMPONENT VARIANCE", 4, kRowH - 2);

        mTunePoorlyRect  = { colW7 * 2, 0, colW7, kRowH };
        mTuneLazyRect    = { colW7 * 3, 0, colW7, kRowH };
        mTunePerfectRect = { colW7 * 4, 0, colW7, kRowH };
