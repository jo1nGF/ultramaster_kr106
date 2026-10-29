#pragma once

#include "PluginProcessor.h"
#include "Controls/KR106Slider.h"
#include "Controls/KR106Button.h"
#include "Controls/KR106Switch.h"
#include "Controls/KR106Knob.h"
#include "Controls/KR106Bender.h"
#include "Controls/KR106Misc.h"
#include "Controls/KR106Scope.h"
#include "Controls/KR106Keyboard.h"
#include "Controls/KR106Tooltip.h"
#include "Controls/KR106PresetDisplay.h"
#include "Controls/KR106MenuSheet.h"
#include "Controls/KR106VarianceSheet.h"
#include "Controls/KR106Touch.h"
#include "Controls/KR106TouchKeys.h"
#include "Controls/KR106LearnBar.h"
#include "Controls/KR106ScaleSheet.h"
#include "Controls/KR106ModEnvDisplay.h"
#include "Controls/KR106SeqPage.h"

class KR106Editor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    KR106Editor(KR106AudioProcessor&);
    ~KR106Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    // Disable JUCE's default Tab/Shift+Tab focus traversal so we can
    // use those keys for MIDI learn navigation in keyPressed().
    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override { return nullptr; }
    void showSettingsMenu();

private:
#if KR106_TOUCH
    // Landscape phone layout (Galaxy S10 is ~870x412 dp). The content is fitted
    // to the window with letterboxing, so other aspect ratios work too.
    static constexpr int kBaseWidth = 870;
    // 412 for the controls + a free strip at the bottom for the MIDI learn bar
    static constexpr int kBaseHeight = 430;
#else
    static constexpr int kBaseWidth = 940;
    static constexpr int kBaseHeight = 224;
#endif

    // Touch layout: section tiles and text labels are painted by the editor
    // (the desktop UI gets them from the background bitmap).
    struct TouchSection { juce::String name; juce::Rectangle<int> r; bool blue; int page; };   // page 0 = Synth, 1 = FX
    struct TouchLabel   { juce::String text; float cx; int y; int page; };
    std::vector<TouchSection> mTouchSections;
    std::vector<TouchLabel> mTouchLabels;
    void paintTouchBackground(juce::Graphics&);
    void showKeysSheet();
    void showScaleSheet();
    std::unique_ptr<KR106TouchKeysSheet> mKeysSheet;
    std::unique_ptr<KR106ScaleSheet> mScaleSheet;
    KR106TouchChoice* mScaleBtn = nullptr;
    kr106touch::LongPress mLongPress;
    KR106LearnBar* mLearnBar = nullptr;   // owned by mControls

    // Touch UI zoom / pan (zoom is relative to the "fit to window" scale)
    float mZoom = 1.f, mTx = 0.f, mTy = 0.f;
    bool mZoomInit = false;
    kr106touch::PanZoom mPanZoom;
    void updateTouchTransform();
    void setTouchZoom(float zoom, juce::Point<float> focalPx);

    // List pop-up used by the arpeggiator pattern / S&H cycle selectors
    std::unique_ptr<KR106MenuSheet> mChoiceMenu;
    KR106TouchChoice* mArpPatternBtn = nullptr;
    KR106TouchChoice* mArpShBtn = nullptr;

    // Two pages: 0 = Synth, 1 = FX (delays, Clouds, limiter, recorder)
    int mPage = 0;
    std::vector<juce::Component*> mPageCtrls[4];
    std::vector<KR106TouchChoice*> mModChoices;      // matrix / LFO selectors, refreshed on the timer
    void showModTargetMenu(int row, juce::Rectangle<int> anchor);
    KR106TouchTabs* mPageTabs = nullptr;
    void setPage(int page);

    KR106TouchChoice* mDDModeBtn = nullptr;
    KR106TouchChoice* mLfoWaveBtn = nullptr;
    KR106TouchToggle* mEnvLoopBtn = nullptr;
    KR106TouchToggle* mArpChordBtn = nullptr;
    KR106TouchToggle* mLimOnBtn = nullptr;
    KR106TouchToggle* mFreezeBtn = nullptr;
    KR106TouchChoice* mArpAutoBtn = nullptr;
    KR106TouchChoice* mArpEveryBtn = nullptr;
    KR106TouchChoice* mArpMaskBtn = nullptr;
    void showArpMaskMenu(juce::Rectangle<int> anchor);
    void showChoiceMenu(const juce::StringArray& names, int current,
                        juce::Rectangle<int> anchor, std::function<void(int)> onPick);

    int qwertyToNote(int keyCode) const;
    void qwertyAllNotesOff();

    int mQwertyBase = 48;  // C3
    bool mQwertyDown[128] = {};

    void timerCallback() override;
    void applyScale(float s, bool resizeWindow = true);
    void advanceMidiLearn(int dir);

    KR106AudioProcessor& mProcessor;
    juce::Image mBackground;
    juce::Component mContent; // inner wrapper; all controls are children of this

    juce::OwnedArray<juce::Component> mControls;
    struct LearnableControl { int paramIdx; juce::Component* ctrl; };
    std::vector<LearnableControl> mLearnableControls; // UI layout order (for Tab navigation)
    std::vector<int> mLearnableParams; // param indices only (parallel, for quick lookup)
    KR106Scope* mScope = nullptr;
    KR106Keyboard* mKeyboard = nullptr;
    KR106PresetDisplay* mPresetDisplay = nullptr;
    KR106ClipLED* mClipLED = nullptr;
    KR106ArpRateSlider* mArpRateSlider = nullptr;
    KR106LfoRateSlider* mLfoRateSlider = nullptr;
    KR106Tooltip mTooltip;

    float mUIScale = 1.f;
    bool mInternalResize = false; // guard: true when applyScale() initiated the resize
    bool mNeedChevronRestore = true;
    bool mWasActive = true;
    int mRepaintDivider = 0;

    juce::Typeface::Ptr mMenuTypeface;
    std::unique_ptr<KR106MenuSheet> mSettingsMenu;
    std::unique_ptr<KR106VarianceSheet> mVarianceSheet;
    std::unique_ptr<juce::Component> mQwertyDiagram;

    void showVarianceSheet();
    void showQwertyDiagram();

    // Click debugger for AU hit-test investigation
    class ClickDebugger : public juce::MouseListener
    {
    public:
        juce::Point<int>& lastClickRef;
        explicit ClickDebugger(juce::Component& rootIn, juce::Point<int>& clickRef)
            : root(rootIn), lastClickRef(clickRef)
        {
            auto logFile = KR106PresetManager::getAppDataDir()
                             .getChildFile("KR106-clicks.log");
            log = std::make_unique<juce::FileLogger>(logFile, "KR-106 click debug", 0);
            log->logMessage("KR-106 " JucePlugin_VersionString
                " / " + juce::SystemStats::getOperatingSystemName()
                + " / host: " + juce::PluginHostType().getHostDescription()
                + " / editor size: " + juce::String(root.getWidth()) + "x" + juce::String(root.getHeight()));
        }
        void logDirect(juce::Point<int> pos)
        {
            log->logMessage("  [editor mouseDown] root: " + juce::String(pos.x) + ", " + juce::String(pos.y));
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            const auto rootPos = e.getEventRelativeTo(&root).getPosition();
            lastClickRef = rootPos;
            root.repaint();
            log->logMessage("=== CLICK (listener) ===");
            log->logMessage("  screen:     " + juce::String(e.getScreenPosition().x) + ", " + juce::String(e.getScreenPosition().y));
            log->logMessage("  root:       " + juce::String(rootPos.x) + ", " + juce::String(rootPos.y));
            log->logMessage("  root size:  " + juce::String(root.getWidth()) + " x " + juce::String(root.getHeight()));
            log->logMessage("  originator: " + describe(e.eventComponent));
            if (auto* hit = root.getComponentAt(rootPos))
            {
                log->logMessage("  hit test:   " + describe(hit));
                int depth = 0;
                for (auto* c = hit; c != nullptr; c = c->getParentComponent())
                {
                    bool selfI = false, childI = false;
                    c->getInterceptsMouseClicks(selfI, childI);
                    log->logMessage("    [" + juce::String(depth++) + "] " + describe(c)
                        + "  bounds=" + c->getBounds().toString()
                        + "  self=" + juce::String((int)selfI) + "  child=" + juce::String((int)childI)
                        + "  visible=" + juce::String((int)c->isVisible()));
                    if (c == &root) break;
                }
            }
            else
                log->logMessage("  hit test:   <nothing>");
        }
    private:
        static juce::String describe(juce::Component* c)
        {
            if (!c) return "<null>";
            auto name = c->getName();
            if (name.isEmpty()) name = "<unnamed>";
            return name + " (" + juce::String(typeid(*c).name()) + ")";
        }
        juce::Component& root;
        std::unique_ptr<juce::FileLogger> log;
    };
    std::unique_ptr<ClickDebugger> mClickDebugger;
    bool mClickDebugEnabled = false;

    // Visual click marker
    juce::Point<int> mLastClickPoint { -1, -1 };
    void paintOverChildren(juce::Graphics& g) override
    {
        if (mClickDebugEnabled && mLastClickPoint.x >= 0)
        {
            g.setColour(juce::Colours::magenta);
            g.fillEllipse((float)mLastClickPoint.x - 6, (float)mLastClickPoint.y - 6, 12, 12);
            g.setColour(juce::Colours::white);
            g.drawEllipse((float)mLastClickPoint.x - 6, (float)mLastClickPoint.y - 6, 12, 12, 1);
        }
    }
};
