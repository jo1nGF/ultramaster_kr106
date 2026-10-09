#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include "../PluginProcessor.h"
#include "KR106MenuSheet.h"

// ============================================================================
// KR106PresetSheet — full-screen overlay showing all 128 presets in 8 columns
// Black background, green 14-segment font. Click to select, Escape to close.
// ============================================================================
class KR106PresetSheet : public juce::Component
{
public:
    static constexpr int kCols = 8;
    static constexpr int kRows = 16;
    static constexpr int kPadX = 4;

    float rowH() const { return getHeight() / static_cast<float>(kRows); }

    KR106PresetSheet(KR106AudioProcessor* proc, juce::Typeface::Ptr typeface,
                     std::function<void()> onClose)
        : mProcessor(proc), mTypeface(typeface), mOnClose(std::move(onClose))
    {
        setWantsKeyboardFocus(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(KR106Theme::bg());

        if (!mProcessor) return;

        int current = mProcessor->getCurrentProgram();
        int num = mProcessor->getNumPrograms();
        float rh = rowH();
        int colW = getWidth() / kCols;

        g.setFont(KR106Theme::ledFont(mTypeface));

        // Grid lines
        // Preset entries (drawn before grid lines so grid renders on top)
        bool searching = mSearchString.isNotEmpty();
        for (int i = 0; i < num && i < kCols * kRows; i++)
        {
            int col = i % kCols;
            int row = i / kCols;
            int x = col * colW;
            int y = juce::roundToInt(row * rh);
            int cellW = (col == kCols - 1) ? (getWidth() - x) : colW;
            int cellH = juce::roundToInt((row + 1) * rh) - y;

            juce::String name = mProcessor->getProgramName(i);
            bool matches = !searching || name.containsIgnoreCase(mSearchString);

            if (matches)
            {
                KR106Theme::drawCell(g, name.substring(0, 16), x, y, cellW, cellH,
                                     i == mHoverIndex, i == current);
            }
            else
            {
                // Dimmed: no hover highlight, disabled text color
                if (i == current)
                {
                    g.setColour(KR106Theme::disabled());
                    g.fillRect(x, y, cellW, cellH);
                    g.setColour(KR106Theme::bg());
                }
                else
                {
                    g.setColour(KR106Theme::disabled());
                }
                g.drawSingleLineText(name.substring(0, 16), x + kPadX, y + cellH - KR106Theme::kTextOffset);
            }
        }

        // Grid lines (on top of cell fills)
        g.setColour(KR106Theme::grid());
        for (int row = 0; row <= kRows; row++)
        {
            int y = juce::roundToInt(row * rh);
            g.drawHorizontalLine(y, 0.f, static_cast<float>(getWidth()));
        }
        for (int col = 1; col < kCols; col++)
        {
            int x = col * colW;
            g.drawVerticalLine(x, 0.f, static_cast<float>(getHeight()));
        }
