#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KR106Tooltip.h"
#include "KR106Touch.h"
#include "PluginProcessor.h"

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

    // Touch UI: knobs are scaled up ~2-2.6x, so the 1x bitmap sprites look soft.
    // Draw them as vectors instead (crisp at any size): value arc + metal cap + pointer.
    void paintVectorKnob(juce::Graphics& g)
    {
        // the arc follows the matrix while a modulation source drives this control
        const float val = juce::jlimit(0.f, 1.f, (mProcessor && mParamIdx >= 0) ? mProcessor->modDisplayValue(mParamIdx)
                                                                              : (mParam ? mParam->getValue() : 0.f));
        const auto c = getLocalBounds().toFloat().getCentre();
        const float rCap = 7.4f, rArc = 11.0f;
        const float a0 = juce::degreesToRadians(-135.f), a1 = juce::degreesToRadians(135.f);
        const float ang = a0 + (a1 - a0) * val;
        const juce::PathStrokeType arcStroke(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc(c.x, c.y, rArc, rArc, 0.f, a0, a1, true);
        g.setColour(juce::Colour(0xff161616));
        g.strokePath(track, arcStroke);
        if (val > 0.004f)
        {
            juce::Path on;
            on.addCentredArc(c.x, c.y, rArc, rArc, 0.f, a0, ang, true);
            g.setColour(juce::Colour(0xffc60a0a));
            g.strokePath(on, arcStroke);
        }

        // soft drop shadow, then the cap
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(c.x - rCap - 0.4f, c.y - rCap + 0.9f, 2.f * (rCap + 0.4f), 2.f * (rCap + 0.4f));
        juce::ColourGradient cap(juce::Colour(0xfff4f4f4), c.x - rCap * 0.45f, c.y - rCap * 0.6f,
                                 juce::Colour(0xff7d7d7d), c.x + rCap * 0.8f, c.y + rCap * 0.9f, true);
        g.setGradientFill(cap);
        g.fillEllipse(c.x - rCap, c.y - rCap, 2.f * rCap, 2.f * rCap);
        g.setColour(juce::Colour(0xff0e0e0e));
        g.drawEllipse(c.x - rCap, c.y - rCap, 2.f * rCap, 2.f * rCap, 0.9f);
        // inner ring for a machined look
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawEllipse(c.x - rCap + 1.3f, c.y - rCap + 1.3f, 2.f * rCap - 2.6f, 2.f * rCap - 2.6f, 0.5f);

        // pointer
        const float dx = std::sin(ang), dy = -std::cos(ang);
        juce::Path ptr;
        ptr.startNewSubPath(c.x + dx * rCap * 0.18f, c.y + dy * rCap * 0.18f);
        ptr.lineTo(c.x + dx * rCap * 0.86f, c.y + dy * rCap * 0.86f);
        g.setColour(juce::Colour(0xff121212));
        g.strokePath(ptr, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paint(juce::Graphics& g) override
    {
        if constexpr (kr106touch::kEnabled)
        {
            paintVectorKnob(g);
            if (mProcessor && mParamIdx >= 0
                && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
            {
                g.setColour(juce::Colour(0, 255, 0));
                g.drawEllipse(getLocalBounds().toFloat().reduced(0.8f), 1.2f);
            }
            return;
        }
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

    void mouseEnter(const juce::MouseEvent&) override
    {
        if (mTooltip && !mDragging && !mEditor)
        {
            updateCCLine();
            mTooltip->show(mParam, this);
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (mTooltip && !mDragging) mTooltip->hide();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        applyCursorWorkaround(e);
    }

    // Touch UI scales knobs up, so restrict the touch area to the visible knob
    // (otherwise neighbouring knobs' transparent frames overlap).
    bool hitTest(int x, int y) override
    {
        if (!kr106touch::kEnabled) return juce::Component::hitTest(x, y);
        return juce::Point<float>((float) x, (float) y)
                   .getDistanceFrom(getLocalBounds().getCentre().toFloat()) <= 10.f;
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
        // Left click cancels MIDI learn if active, then falls through to start drag
        if (mProcessor && mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) >= 0)
        {
            mProcessor->cancelMidiLearn();
            if (mTooltip) mTooltip->hide();
            repaint();
        }
        mDragging = true;
        mAccumVal = mParam ? mParam->getValue() : 0.f;
        mLastRawDelta = 0.f;
        if (mParam) mParam->beginChangeGesture();
        if (mTooltip) mTooltip->show(mParam, this);
        setMouseCursor(juce::MouseCursor::NoCursor);
        e.source.enableUnboundedMouseMovement(true);
        if (isAppleHost()) mCursorDirty = true;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        applyCursorWorkaround(e);
        if (!mParam) return;

        float offsetY = (float)e.getOffsetFromDragStart().y;
        float offsetX = (float)e.getOffsetFromDragStart().x;

        // Manhattan drag: -(dY - dX) so up+right = increase
        float rawDelta = -(offsetY - offsetX);
        float increment = rawDelta - mLastRawDelta;
        mLastRawDelta = rawDelta;

        // Cumulative gearing: shift only affects new movement
        // Scale by display DPI so non-retina screens get the same physical throw
        float dpiScale = std::max(1.f, static_cast<float>(getTopLevelComponent()->getDesktopScaleFactor()));
        // Touch: shorter throw, scaled by this knob's own gearing ratio
        float gearing = kr106touch::kEnabled ? kr106touch::kKnobThrow * (mGearing / 127.f) : mGearing;
        if (e.mods.isCommandDown()) gearing *= 100.f;
        else if (e.mods.isShiftDown()) gearing *= 10.f;
        if (!kr106touch::kEnabled) gearing /= dpiScale;
        mAccumVal = juce::jlimit(0.f, 1.f, mAccumVal + increment / gearing);
        float newVal = mAccumVal;

        // Three-zone center snap: 0-99px = -100..-1 cents,
        // 100-105px = 0 cents (detent), 106-205px = +1..+100 cents.
        // Maps the 0..1 accumulator through a piecewise function that
        // creates a dead zone at center without skipping any values.
        if (mSnapCenter)
        {
            // Accumulator pixel position (0..205 across 0..1 range)
            float px = mAccumVal * gearing;
            float half = gearing * 0.5f;       // 102.5
            float snapHalf = 3.f;              // 3px each side of center
            float negEnd = half - snapHalf;    // 99.5
            float posStart = half + snapHalf;  // 105.5

            if (px <= negEnd)
            {
                // -100..-1 cents: map 0..99.5 to 0..0.495
                newVal = (px / negEnd) * 0.495f;
            }
            else if (px >= posStart)
            {
                // +1..+100 cents: map 105.5..205 to 0.505..1.0
                newVal = 0.505f + ((px - posStart) / (gearing - posStart)) * 0.495f;
            }
            else
            {
                newVal = 0.5f; // snap to 0 cents
                if (!mInSnapZone)
                {
                    mAccumVal = 0.5f; // reset once on entry
                    mInSnapZone = true;
                }
            }
            if (px <= negEnd || px >= posStart)
                mInSnapZone = false;
        }

        mParam->setValueNotifyingHost(newVal);
        if (mTooltip) mTooltip->update();
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& /*e*/) override
    {
        if (!mParam) return;
        if (mEditor) dismissEdit();
        if (mTooltip) mTooltip->hide();

        auto* parent = mTooltip ? mTooltip->getParentComponent() : getTopLevelComponent();
        if (!parent) return;

        mEditor = std::make_unique<juce::TextEditor>();
        mEditor->setLookAndFeel(&getEditBoxLnF());
        mEditor->setFont(juce::FontOptions(11.f));
        mEditor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0, 0, 0));
        mEditor->setColour(juce::TextEditor::textColourId, juce::Colour(255, 255, 255));
        mEditor->setColour(juce::TextEditor::outlineColourId, juce::Colour(128, 128, 128));
        mEditor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(128, 128, 128));
        mEditor->setColour(juce::TextEditor::highlightColourId, juce::Colour(80, 80, 80));
        mEditor->setJustification(juce::Justification::centred);

        juce::String displayText = mParam->getCurrentValueAsText();
        mEditor->setText(displayText, false);

        auto srcBounds = parent->getLocalArea(getParentComponent(), getBounds());
        juce::Font font{juce::FontOptions(11.f)};
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, displayText, 0.f, 0.f);
        int tw = std::max(50, juce::roundToInt(glyphs.getBoundingBox(0, -1, false).getWidth()) + 16);
        int th = 16;
        int tx = srcBounds.getCentreX() - tw / 2;
        int ty = srcBounds.getBottom() + 2;
        auto pb = parent->getLocalBounds();
        tx = juce::jlimit(0, pb.getWidth() - tw, tx);
        ty = juce::jmin(pb.getHeight() - th, ty);
        mEditor->setBounds(tx, ty, tw, th);

        parent->addAndMakeVisible(mEditor.get());
        mEditor->grabKeyboardFocus();

        int numEnd = findNumericEnd(displayText);
        mEditor->setHighlightedRegion(juce::Range<int>(0, numEnd));

        mEditor->onReturnKey = [this]() { commitEdit(); };
        mEditor->onEscapeKey = [this]() { dismissEdit(); };
        mEditor->onFocusLost = [this]() { dismissEdit(); };
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!mDragging) return;
        mDragging = false;
        if (mParam) mParam->endChangeGesture();
        if (mTooltip) mTooltip->hide();
        setMouseCursor(juce::MouseCursor::NormalCursor);
        e.source.enableUnboundedMouseMovement(false);

        // Warp cursor to centre of knob so it appears on the control after release
        auto screenPos = localPointToGlobal(juce::Point<int>(getWidth() / 2, getHeight() / 2));
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(screenPos.toFloat());
        if (isAppleHost()) mCursorDirty = true;

        if (onDragEnd) onDragEnd();
    }

    void updateCCLine()
    {
        if (!mTooltip) return;
        if (!mProcessor || mParamIdx < 0) { mTooltip->setLine2({}); return; }
        if (mProcessor->mMidiLearnParam.load(std::memory_order_relaxed) == mParamIdx)
        {
            int cc = mProcessor->getCCForParam(mParamIdx);
            mTooltip->setLine2(cc >= 0 ? "MIDI LEARN (CC " + juce::String(cc) + ")" : "MIDI LEARN");
            return;
        }
        int cc = mProcessor->getCCForParam(mParamIdx);
        mTooltip->setLine2(cc >= 0 ? "CC " + juce::String(cc) : "CC ??");
    }

private:
    void commitEdit()
    {
        if (!mEditor || !mParam) return;
        juce::String raw = mEditor->getText().trim();
        if (raw.isEmpty()) { dismissEdit(); return; }
        float normalized = juce::jlimit(0.f, 1.f, mParam->getValueForText(raw));
        mParam->beginChangeGesture();
        mParam->setValueNotifyingHost(normalized);
        mParam->endChangeGesture();
        repaint();
        dismissEdit();
    }

    void dismissEdit()
    {
        if (!mEditor) return;
        auto editor = std::move(mEditor);
        editor->setLookAndFeel(nullptr);
        editor->onFocusLost = nullptr;
        if (auto* parent = editor->getParentComponent())
            parent->removeChildComponent(editor.get());
    }

    struct EditBoxLnF : juce::LookAndFeel_V4
    {
        void drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor&) override
        {
            g.setColour(juce::Colour(128, 128, 128));
            g.drawRect(0, 0, w, h, 1);
        }
    };
    static EditBoxLnF& getEditBoxLnF() { static EditBoxLnF lnf; return lnf; }

    static int findNumericEnd(const juce::String& text)
    {
        int i = 0, len = text.length();
        if (i < len && (text[i] == '+' || text[i] == '-')) i++;
        bool hasDigit = false;
        while (i < len && (juce::CharacterFunctions::isDigit(text[i]) || text[i] == '.'))
        { if (text[i] != '.') hasDigit = true; i++; }
        return hasDigit ? i : len;
    }

    juce::RangedAudioParameter* mParam = nullptr;
    juce::Image mSpriteSheet;
    KR106Tooltip* mTooltip = nullptr;
    KR106AudioProcessor* mProcessor = nullptr;
    int mParamIdx = -1;
    int mNumFrames = 32;
    juce::Image mBgImage;
    float mAccumVal = 0.f;
    float mLastRawDelta = 0.f;
    bool mDragging = false;
    bool mCursorDirty = false;
    bool mSnapCenter = false;
    bool mInSnapZone = false;
    float mGearing = 127.f;
    std::unique_ptr<juce::TextEditor> mEditor;

    void applyCursorWorkaround(const juce::MouseEvent& e)
    {
        if (!mCursorDirty) return;
        mCursorDirty = false;
        if (auto* source = juce::Desktop::getInstance().getMouseSource(e.source.getIndex()))
        {
            source->showMouseCursor(getMouseCursor());
            source->forceMouseCursorUpdate();
        }
    }
};
