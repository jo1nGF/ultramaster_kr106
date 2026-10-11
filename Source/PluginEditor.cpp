#include "PluginEditor.h"
#include "BinaryData.h"
#include "Controls/KR106QwertyDiagram.h"

#define KR106_DEBUG_RESIZE 0
#if KR106_DEBUG_RESIZE
#include "KR106PresetManager.h"
static void dbgResize(const juce::String& msg)
{
    auto f = KR106PresetManager::getAppDataDir().getChildFile("resize.log");
    f.appendText(juce::Time::getCurrentTime().toString(true, true, true, true) + "  " + msg + "\n");
}
#else
static void dbgResize(const juce::String&) {}
#endif

KR106Editor::KR106Editor(KR106AudioProcessor& p)
    : AudioProcessorEditor(p), mProcessor(p)
{
    // Restore saved scale, or auto-detect: 2x on non-HiDPI, 1x on Retina.
    if (p.mUIScale > 0.f)
        mUIScale = p.mUIScale;
    else
    {
        auto& displays = juce::Desktop::getInstance().getDisplays();
        auto* display = displays.getPrimaryDisplay();
        mUIScale = (display && display->scale < 1.5) ? 2.f : 1.f;
        p.mUIScale = mUIScale; // persist so it survives window recreation
    }

    // Content wrapper: all controls are children of this component.
    // We apply the UI scale transform to mContent, leaving the editor's
    // own transform free for the host's DPI scaling (setScaleFactor).
    addAndMakeVisible(mContent);
    mContent.setInterceptsMouseClicks(false, true); // pass clicks through, but let children handle theirs
    mContent.setBounds(0, 0, kBaseWidth, kBaseHeight);

    // Load @2x images from binary data
    auto loadImg = [](const void* data, int size) {
        return juce::ImageCache::getFromMemory(data, size);
    };

    mBackground = loadImg(BinaryData::kr106_background2x_png,
                          BinaryData::kr106_background2x_pngSize);

    auto switchV    = loadImg(BinaryData::switch_3way2x_png,
                              BinaryData::switch_3way2x_pngSize);
    auto switchH    = loadImg(BinaryData::switch_3way_horizontal2x_png,
                              BinaryData::switch_3way_horizontal2x_pngSize);
    auto smallKnob  = loadImg(BinaryData::smallknob2x_png,
                              BinaryData::smallknob2x_pngSize);
    auto ledRed     = loadImg(BinaryData::led_red2x_png,
                              BinaryData::led_red2x_pngSize);
    auto benderGrad = loadImg(BinaryData::kr106_bender_gradient2x_png,
                              BinaryData::kr106_bender_gradient2x_pngSize);
    auto chevron    = loadImg(BinaryData::transpose_chevron2x_png,
                              BinaryData::transpose_chevron2x_pngSize);
    auto newKnob    = loadImg(BinaryData::new_knob2x_png,
                              BinaryData::new_knob2x_pngSize);
    auto sliderHdl  = loadImg(BinaryData::slider_handle2x_png,
                              BinaryData::slider_handle2x_pngSize);

    auto param = [&](int idx) { return p.getParam(idx); };
    auto* tip = &mTooltip;

    // Helper: create control, set bounds, add as visible child, store in array
    // Automatically wires up MIDI learn for sliders that have a param.
    auto add = [&](auto* ctrl, int x, int y, int w, int h) {
        ctrl->setBounds(x, y, w, h);
        mContent.addAndMakeVisible(ctrl);
        mControls.add(ctrl);
        return ctrl;
    };

    // Helper: create slider with MIDI learn wired up
    auto addSlider = [&](int paramIdx, auto* ctrl, int x, int y, int w, int h) {
        ctrl->setMidiLearn(&p, paramIdx);
        mLearnableParams.push_back(paramIdx);
        mLearnableControls.push_back({paramIdx, ctrl});
        return add(ctrl, x, y, w, h);
    };

    // Helper: create switch/button with MIDI learn wired up
    auto addML = [&](int paramIdx, auto* ctrl, int x, int y, int w, int h) {
        ctrl->setMidiLearn(&p, paramIdx);
        mLearnableParams.push_back(paramIdx);
        mLearnableControls.push_back({paramIdx, ctrl});
        return add(ctrl, x, y, w, h);
    };

#if KR106_TOUCH
    // =====================================================================
    // TOUCH LAYOUT (landscape, 870x412 base units)
    // Controls keep their native geometry and are scaled up with a
    // per-component transform, so all drawing/drag code is reused as-is.
    // =====================================================================
    constexpr float kSF = 2.1f;   // faders      (20x49  -> 42x103)
    constexpr float kBF = 1.9f;   // LED buttons (17x28  -> 32x53)
    constexpr float kWF = 2.0f;   // 3-way switches (9x24 -> 18x48)
    constexpr float kKF = 1.6f;   // knobs       (28x27  -> 45x43)
    constexpr int kTileH = 156, kHdr = 20, kRowA = 74, kRowB = 238;
    constexpr int kFadeY = 28;    // fader top, relative to tile
    constexpr int kLblY = 148;    // label baseline, relative to tile

    // JUCE applies a component's transform relative to its parent's origin, so
    // position is folded into the transform: bounds = (0,0,w,h), transform = scale + translate.
    auto scaled = [&](auto* ctrl, float f) {
        auto b = ctrl->getBounds();
        ctrl->setBounds(0, 0, b.getWidth(), b.getHeight());
        ctrl->setTransform(juce::AffineTransform::scale(f).translated((float) b.getX(), (float) b.getY()));
        return ctrl;
    };
    auto lbl = [&](const char* t, float cx, int y, int group = 0) { mTouchLabels.push_back({ t, cx, y, group }); };
    auto tile = [&](const char* name, int x, int y, int w, bool blue) {
        mTouchSections.push_back({ name, { x, y, w, kTileH }, blue });
    };
    // Control helpers: x = left edge in base units, ty = tile top
    auto fader = [&](int idx, int x, int ty, const char* l) {
        auto* sl = new KR106Slider(param(idx), tip, sliderHdl);
        addSlider(idx, sl, x, ty + kFadeY, 20, 49)->setExtraRight(1);
        scaled(sl, kSF); lbl(l, x + 20.f, ty + kLblY);
        return sl;
    };
    // lblY < 0: switch centred beside a fader, label on the common baseline.
    // lblY >= 0: switch aligned with the LED buttons, label directly below it.
    auto sw3 = [&](int idx, int n, int x, int ty, const char* l, int lblY = -1) {
        const bool beside = lblY < 0;
        auto* c = new KR106Switch(param(idx), switchV, n, tip);
        addML(idx, c, x, ty + kFadeY + (beside ? 28 : 8), 9, 24); scaled(c, kWF);
        lbl(l, x + 9.f, ty + (beside ? kLblY : lblY));
    };
    auto ledBtn = [&](int idx, int type, int x, int ty, const char* l, int labelY) {
        auto* c = new KR106ButtonLED(param(idx), type, ledRed, param(kPower), tip);
        addML(idx, c, x, ty + kFadeY + 8, 17, 28); scaled(c, kBF);
        lbl(l, x + 16.f, ty + labelY);
    };
    auto knob = [&](int idx, int cx, int cy, const char* l, float f, int group = 0) {
        auto* c = new KR106Knob(param(idx), smallKnob, tip, 32);
        c->setMidiLearn(&p, idx);
        mLearnableParams.push_back(idx); mLearnableControls.push_back({ idx, c });
        add(c, cx - (int) (14 * f), cy - (int) (13.5f * f), 28, 27); scaled(c, f);
        lbl(l, (float) cx, cy + (int) (11.f * f) + 11, group);   // label sits below the value arc (radius 11)
        if (group > 0) mDelayTabCtrls[group - 1].push_back(c);
        return c;
    };

    // ---- widths: row A (LFO, DCO, HPF, VCF) and row B (VCA, ENV, CHORUS, DELAY, ARP)
    const int wA[4] = { 130, 348, 56, 262 };   // DCO: Sub button ends at x0+340
    const int wB[5] = { 86, 188, 88, 252, 230 };
    auto gapFor = [&](const int* w, int n) {
        int sum = 0; for (int i = 0; i < n; i++) sum += w[i];
        return std::max(2, (kBaseWidth - 16 - sum) / (n - 1));
    };
    const int gA = gapFor(wA, 4), gB = gapFor(wB, 5);
    int xA[4], xB[5];
    { int x = 8; for (int i = 0; i < 4; i++) { xA[i] = x; x += wA[i] + gA; } }
    { int x = 8; for (int i = 0; i < 5; i++) { xB[i] = x; x += wB[i] + gB; } }

    // ---------------- TOP BAR ----------------
    scaled(add(new KR106PowerSwitch(param(kPower)), 12, 17, 15, 19), 1.6f);
    lbl("Power", 24.f, 58);

    mPresetDisplay = add(new KR106PresetDisplay(&p), 46, 21, 128, 14);
    scaled(mPresetDisplay, 1.7f);
    {
        static const char* dbSvg =
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6a8 3 0 1 0 16 0a8 3 0 1 0 -16 0" /><path d="M4 6v6a8 3 0 0 0 16 0v-6" /><path d="M4 12v6a8 3 0 0 0 16 0v-6" /></svg>)";
        static const char* gearSvg =
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 -1.065" /><path d="M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" /></svg>)";
        add(new KR106IconButton(dbSvg, [this]() { mPresetDisplay->openContextMenu(); }), 266, 17, 30, 30);
        add(new KR106IconButton(gearSvg, [this]() { showSettingsMenu(); }), 828, 17, 30, 30);
    }
    mScope = add(new KR106Scope(&p), 306, 3, 128, 74);
    scaled(mScope, 0.82f);

    auto* tune = knob(kTuning, 456, 29, "Tune", 1.7f);
    tune->setSnapCenter(true); tune->setGearing(205.f);
    knob(kMasterVol, 504, 29, "Master", 1.7f);
    mClipLED = dynamic_cast<KR106ClipLED*>(add(new KR106ClipLED(ledRed, 1.f), 524, 5, 9, 9));
    knob(kPortaRate, 552, 29, "Porta", 1.7f);
    {   auto* c = new KR106Switch(param(kPortaMode), switchV, 3, tip);
        addML(kPortaMode, c, 586, 14, 9, 24); scaled(c, 1.5f); lbl("Mode", 593.f, 58); }
    {   auto* c = new KR106HorizontalSwitch(param(kAdsrMode), switchH, 2, tip);
        addML(kAdsrMode, c, 610, 22, 24, 9); scaled(c, 1.9f); lbl("60 / 106", 633.f, 58); }
    knob(kBenderDco, 682, 29, "Bnd dco", 1.7f);
    knob(kBenderVcf, 722, 29, "Bnd vcf", 1.7f);
    knob(kBenderLfo, 762, 29, "Bnd lfo", 1.7f);
    add(new KR106TouchTextButton("Keys", [this]() { showKeysSheet(); }, juce::Colour(0xff9a0e0e)), 792, 18, 32, 30);

    // ---------------- ROW A ----------------
    {   // LFO
        int x0 = xA[0], ty = kRowA; tile("LFO", x0, ty, wA[0], false);
        mLfoRateSlider = new KR106LfoRateSlider(param(kLfoRate), param(kLfoQuantize), kLfoRate, kLfoQuantize, tip, sliderHdl, &p.mLfoSyncHost);
        addSlider(kLfoRate, mLfoRateSlider, x0 + 8, ty + kFadeY, 20, 49)->setExtraRight(1);
        scaled(mLfoRateSlider, kSF); lbl("Rate", x0 + 28.f, ty + kLblY);
        fader(kLfoDelay, x0 + 52, ty, "Delay");
        sw3(kLfoMode, 2, x0 + 100, ty, "Mode");
    }
    {   // DCO
        int x0 = xA[1], ty = kRowA; tile("DCO", x0, ty, wA[1], false);
        fader(kDcoLfo, x0 + 8, ty, "LFO");
        fader(kDcoPwm, x0 + 52, ty, "PWM");
        sw3(kPwmMode, 3, x0 + 100, ty, "Mode");
        fader(kDcoSub, x0 + 136, ty, "Sub");
        fader(kDcoNoise, x0 + 180, ty, "Noise");
        ledBtn(kDcoPulse, 1, x0 + 232, ty, "Pulse", 104);
        ledBtn(kDcoSaw,   1, x0 + 232 + 38, ty, "Saw", 104);
        ledBtn(kDcoSubSw, 2, x0 + 232 + 76, ty, "Sub", 104);
        auto* oct = new KR106HorizontalSwitch(param(kOctTranspose), switchH, 3, tip);
        addML(kOctTranspose, oct, x0 + 252, ty + 114, 24, 9); scaled(oct, 2.0f);
        lbl("Range", x0 + 276.f, ty + kLblY);
    }
    {   // HPF (4-position slider with model-dependent labels)
        int x0 = xA[2], ty = kRowA; tile("HPF", x0, ty, wA[2], false);
        auto* s = new KR106HPFSlider(param(kHpfFreq), tip, sliderHdl, &p.mDSP.mSynthModel);
        s->setMidiLearn(&p, kHpfFreq);
        mLearnableParams.push_back(kHpfFreq); mLearnableControls.push_back({ kHpfFreq, s });
        add(s, x0 + 8, ty + kFadeY, 20, 49); s->setExtraRight(1); scaled(s, kSF);
        lbl("Freq", x0 + 28.f, ty + kLblY);
    }
    {   // VCF
        int x0 = xA[3], ty = kRowA; tile("VCF", x0, ty, wA[3], false);
        fader(kVcfFreq, x0 + 8, ty, "Freq");
        fader(kVcfRes, x0 + 52, ty, "Res");
        fader(kVcfEnv, x0 + 96, ty, "Env");
        fader(kVcfLfo, x0 + 140, ty, "LFO");
        fader(kVcfKbd, x0 + 184, ty, "Kbd");
        sw3(kVcfEnvInv, 2, x0 + 232, ty, "Inv");
    }

    // ---------------- ROW B ----------------
    {   // VCA
        int x0 = xB[0], ty = kRowB; tile("VCA", x0, ty, wB[0], false);
        sw3(kVcaMode, 2, x0 + 8, ty, "Mode");
        fader(kVcaLevel, x0 + 36, ty, "Level");
    }
    {   // ENVELOPE (the 60/106 switch lives in the top bar)
        int x0 = xB[1], ty = kRowB; tile("Envelope", x0, ty, wB[1], false);
        fader(kEnvA, x0 + 8, ty, "A");
        fader(kEnvD, x0 + 52, ty, "D");
        fader(kEnvS, x0 + 96, ty, "S");
        fader(kEnvR, x0 + 140, ty, "R");
    }
    {   // CHORUS
        int x0 = xB[2], ty = kRowB; tile("Chorus", x0, ty, wB[2], true);
        ledBtn(kChorusI,  1, x0 + 8,  ty, "I",  kLblY);
        ledBtn(kChorusII, 2, x0 + 46, ty, "II", kLblY);
    }
    {   // DELAY tile with two tabs. Tape: Wow = depth of the delay-time LFO (pitch wobble),
        // Speed = its rate. Digital: clean stereo echo with Stereo / Ping-pong / Dotted modes.
        int x0 = xB[3], ty = kRowB; tile("", x0, ty, wB[3], true);
        mDelayTabs = add(new KR106TouchTabs(juce::StringArray { "Tape delay", "Digital delay" },
                                            [this](int i) { setDelayTab(i + 1); }),
                         x0 + 6, ty + 1, wB[3] - 12, 18);
        const int cy = ty + 68;
        knob(kDelayTime,     x0 + 26,  cy, "Time",  1.75f, 1);
        knob(kDelayFeedback, x0 + 66,  cy, "Fdbk",  1.75f, 1);
        knob(kDelayMix,      x0 + 106, cy, "Mix",   1.75f, 1);
        knob(kDelayTone,     x0 + 146, cy, "Tone",  1.75f, 1);
        knob(kDelayWow,      x0 + 186, cy, "Wow",   1.75f, 1);
        knob(kDelayWowRate,  x0 + 226, cy, "Speed", 1.75f, 1);

        knob(kDDelayTime,     x0 + 26,  cy, "Time",    1.75f, 2);
        knob(kDDelayFeedback, x0 + 66,  cy, "Fdbk",    1.75f, 2);
        knob(kDDelayMix,      x0 + 106, cy, "Mix",     1.75f, 2);
        knob(kDDelayTone,     x0 + 146, cy, "Tone",    1.75f, 2);
        knob(kDDelayLowCut,   x0 + 186, cy, "Low cut", 1.75f, 2);
        mDDModeBtn = add(new KR106TouchChoice("Mode",
            [this]() { return juce::String(kr106::DigitalDelay::kModeNames[kr106::DigitalDelay::ModeFromValue(
                          mProcessor.getParam(kDDelayMode)->getValue())]); },
            [this]() {
                int m = kr106::DigitalDelay::ModeFromValue(mProcessor.getParam(kDDelayMode)->getValue());
                mProcessor.getParam(kDDelayMode)->setValueNotifyingHost(
                    kr106::DigitalDelay::ValueFromMode((m + 1) % kr106::DigitalDelay::kNumModes));
                mDDModeBtn->refresh(); }),
            x0 + 204, cy - 18, 44, 36);
        mDelayTabCtrls[1].push_back(mDDModeBtn);
        setDelayTab(1);
    }
    {   // ARPEGGIATOR: classic controls on top, pattern + S&H cycle selectors below
        int x0 = xB[4], ty = kRowB; tile("Arp", x0, ty, wB[4], true);
        mArpRateSlider = new KR106ArpRateSlider(param(kArpRate), param(kArpQuantize), kArpRate, kArpQuantize, tip, sliderHdl, &p.mArpSyncHost);
        addSlider(kArpRate, mArpRateSlider, x0 + 8, ty + kFadeY, 20, 49)->setExtraRight(1);
        scaled(mArpRateSlider, kSF); lbl("Rate", x0 + 28.f, ty + kLblY);
        ledBtn(kHold,     1, x0 + 54,  ty, "Hold", 100);
        ledBtn(kArpeggio, 1, x0 + 92,  ty, "Arp",  100);
        sw3(kArpMode, 3, x0 + 142, ty, "Mode", 100);
        sw3(kArpRange, 3, x0 + 182, ty, "Range", 100);

        auto patName = [this]() {
            return juce::String(kr106::kPatternNames[kr106::patternFromValue(mProcessor.getParam(kArpPattern)->getValue())]);
        };
        auto shName = [this]() {
            return juce::String(kr106::kShLoops[kr106::shLoopFromValue(mProcessor.getParam(kArpShLoop)->getValue())].name);
        };
        mArpPatternBtn = add(new KR106TouchChoice("Pattern", patName, [this, x0, ty]() {
            juce::StringArray names;
            for (int i = 0; i < kr106::kNumArpPatterns; i++) names.add(kr106::kPatternNames[i]);
            showChoiceMenu(names, kr106::patternFromValue(mProcessor.getParam(kArpPattern)->getValue()),
                           { x0 + 54, ty + 108, 108, 30 },
                           [this](int idx) {
                               mProcessor.getParam(kArpPattern)->setValueNotifyingHost(kr106::valueFromPattern(idx));
                               mArpPatternBtn->refresh(); });
        }), x0 + 54, ty + 110, 108, 30);
        mArpShBtn = add(new KR106TouchChoice("S&H cycle", shName, [this, x0, ty]() {
            juce::StringArray names;
            for (int i = 0; i < kr106::kNumShLoops; i++) names.add(kr106::kShLoops[i].name);
            showChoiceMenu(names, kr106::shLoopFromValue(mProcessor.getParam(kArpShLoop)->getValue()),
                           { x0 + 166, ty + 108, 60, 30 },
                           [this](int idx) {
                               mProcessor.getParam(kArpShLoop)->setValueNotifyingHost(kr106::valueFromShLoop(idx));
                               mArpShBtn->refresh(); });
        }), x0 + 166, ty + 110, 60, 30);
    }

    // Hidden desktop keyboard object: the editor's timer / QWERTY code updates it.
    // The touch UI uses the on-screen keys sheet instead (Keys button).
    mKeyboard = new KR106Keyboard(&p, chevron);
    mControls.add(mKeyboard);
    mKeyboard->setBounds(0, 0, 792, 114);

    // Long-press on any control => MIDI learn (the touch equivalent of right-click)
    mLongPress.onLongPress = [this](juce::Component* c) {
        for (auto& lc : mLearnableControls)
            if (lc.ctrl == c)
            {
                mProcessor.startMidiLearn(lc.paramIdx);
                c->repaint();
                mTooltip.showText("MIDI learn: move a control on your keyboard", c);
                return true;
            }
        return false;
    };
    mContent.addMouseListener(&mLongPress, true);

    // Zoom / pan: drag on any empty area, pinch with two fingers on empty areas,
    // double-tap empty area to toggle 100% <-> 200%.
    mPanZoom.isBackground = [this](juce::Component* c) { return c == &mContent || c == this; };
    mPanZoom.toLocal = [this](juce::Point<float> sp) { return getLocalPoint(nullptr, sp); };
    mPanZoom.onPan = [this](juce::Point<float> d) { mTx += d.x; mTy += d.y; updateTouchTransform(); };
    mPanZoom.onPinch = [this](float f, juce::Point<float> focal) { setTouchZoom(mZoom * f, focal); };
    mPanZoom.onDoubleTap = [this](juce::Point<float> p) { setTouchZoom(mZoom > 1.05f ? 1.f : 2.f, p); };
    addMouseListener(&mPanZoom, true);
#else
    // Power section
    add(new KR106PowerSwitch(param(kPower)),              46,  40,  15, 19);
    // Performance controls — created here for visual layout, but registered
    // for MIDI learn tab order after Chorus II (end of synth panel).
    auto* kTuningKnob = new KR106Knob(param(kTuning), smallKnob, tip, 32);
    kTuningKnob->setMidiLearn(&p, kTuning);
    kTuningKnob->setSnapCenter(true);
    kTuningKnob->setGearing(205.f); // ~1px/cent, 5px snap zone at center
    add(kTuningKnob, 40, 64, 28, 27);

    auto* kMasterVolKnob = new KR106Knob(param(kMasterVol), smallKnob, tip, 32);
    kMasterVolKnob->setMidiLearn(&p, kMasterVol);
    add(kMasterVolKnob, 30, 118, 28, 27);
    mClipLED = dynamic_cast<KR106ClipLED*>(
        add(new KR106ClipLED(ledRed, 1.f), 53, 127, 9, 9));
    auto* kPortaRateKnob = new KR106Knob(param(kPortaRate), smallKnob, tip, 32);
    kPortaRateKnob->setMidiLearn(&p, kPortaRate);
    add(kPortaRateKnob, 66, 118, 28, 27);

    auto* kBenderDcoSlider = new KR106Slider(param(kBenderDco), tip, sliderHdl);
    kBenderDcoSlider->setMidiLearn(&p, kBenderDco);
    add(kBenderDcoSlider, 34, 147, 20, 49)->setExtraRight(1);
    auto* kBenderVcfSlider = new KR106Slider(param(kBenderVcf), tip, sliderHdl);
    kBenderVcfSlider->setMidiLearn(&p, kBenderVcf);
    add(kBenderVcfSlider, 52, 147, 20, 49)->setExtraRight(1);
    auto* kBenderLfoSlider = new KR106Slider(param(kBenderLfo), tip, sliderHdl);
    kBenderLfoSlider->setMidiLearn(&p, kBenderLfo);
    add(kBenderLfoSlider, 70, 147, 19, 49);

    auto* kPortaModeSwitch = new KR106Switch(param(kPortaMode), switchV, 3, tip);
    kPortaModeSwitch->setMidiLearn(&p, kPortaMode);
    add(kPortaModeSwitch, 92, 161, 9, 24);

    // Pitch bend lever with vertical LFO trigger
    add(new KR106Bender(param(kBender), benderGrad, &p),   66, 200,  60, 12);

    // === ARPEGGIATOR SECTION ===
    addML(kTranspose, new KR106ButtonLED(param(kTranspose), 1, ledRed, param(kPower), tip),  95, 43, 17, 28);
    addML(kHold,      new KR106ButtonLED(param(kHold),      1, ledRed, param(kPower), tip), 122, 43, 17, 28);
    addML(kArpeggio,  new KR106ButtonLED(param(kArpeggio),  1, ledRed, param(kPower), tip), 154, 43, 17, 28);

    addML(kArpMode,  new KR106Switch(param(kArpMode),  switchV, 3, tip), 175, 46, 9, 24);
    addML(kArpRange, new KR106Switch(param(kArpRange), switchV, 3, tip), 210, 46, 9, 24);
    mArpRateSlider = new KR106ArpRateSlider(param(kArpRate), param(kArpQuantize), kArpRate, kArpQuantize, tip, sliderHdl, &p.mArpSyncHost);
    addSlider(kArpRate, mArpRateSlider, 227, 33, 19, 49);

    // === LFO SECTION ===
    mLfoRateSlider = new KR106LfoRateSlider(param(kLfoRate), param(kLfoQuantize), kLfoRate, kLfoQuantize, tip, sliderHdl, &p.mLfoSyncHost);
    addSlider(kLfoRate, mLfoRateSlider, 249, 33, 20, 49)->setExtraRight(1);
    addSlider(kLfoDelay, new KR106Slider(param(kLfoDelay), tip, sliderHdl), 267, 33, 19, 49);
    addML(kLfoMode, new KR106Switch(param(kLfoMode), switchV, 2, tip),   294, 46,  9, 24);

    // === DCO SECTION ===
    addSlider(kDcoLfo, new KR106Slider(param(kDcoLfo), tip, sliderHdl), 313, 33, 20, 49)->setExtraRight(1);
    addSlider(kDcoPwm, new KR106Slider(param(kDcoPwm), tip, sliderHdl), 331, 33, 19, 49);
    addML(kPwmMode, new KR106Switch(param(kPwmMode), switchV, 3, tip),   351, 46,  9, 24);

    // DCO waveform buttons+LEDs
    addML(kDcoPulse, new KR106ButtonLED(param(kDcoPulse), 1, ledRed, param(kPower), tip), 377, 43, 17, 28);
    addML(kDcoSaw,   new KR106ButtonLED(param(kDcoSaw),   1, ledRed, param(kPower), tip), 393, 43, 17, 28);
    addML(kDcoSubSw, new KR106ButtonLED(param(kDcoSubSw), 2, ledRed, param(kPower), tip), 409, 43, 17, 28);

    // Octave transpose horizontal switch (under DCO buttons)
    addML(kOctTranspose, new KR106HorizontalSwitch(param(kOctTranspose), switchH, 3, tip), 389, 84, 24, 9);

    // DCO Sub/Noise sliders
    addSlider(kDcoSub, new KR106Slider(param(kDcoSub), tip, sliderHdl), 427, 33, 20, 49)->setExtraRight(1);
    addSlider(kDcoNoise, new KR106Slider(param(kDcoNoise), tip, sliderHdl), 445, 33, 19, 49);

    // === HPF SECTION ===
    { auto* s = new KR106HPFSlider(param(kHpfFreq), tip, sliderHdl, &p.mDSP.mSynthModel);
      s->setMidiLearn(&p, kHpfFreq);
      mLearnableParams.push_back(kHpfFreq);
      mLearnableControls.push_back({kHpfFreq, s});
      add(s, 470, 33, 19, 49); }

    // === VCF SECTION ===
    addSlider(kVcfFreq, new KR106Slider(param(kVcfFreq), tip, sliderHdl), 495, 33, 20, 49)->setExtraRight(1);
    addSlider(kVcfRes, new KR106Slider(param(kVcfRes), tip, sliderHdl), 513, 33, 19, 49);
    addML(kVcfEnvInv, new KR106Switch(param(kVcfEnvInv), switchV, 2, tip), 536, 46,  9, 24);
    addSlider(kVcfEnv, new KR106Slider(param(kVcfEnv), tip, sliderHdl), 549, 33, 20, 49)->setExtraRight(1);
    addSlider(kVcfLfo, new KR106Slider(param(kVcfLfo), tip, sliderHdl), 567, 33, 20, 49)->setExtraRight(1);
    addSlider(kVcfKbd, new KR106Slider(param(kVcfKbd), tip, sliderHdl), 585, 33, 19, 49);

    // === VCA SECTION ===
    addML(kVcaMode, new KR106Switch(param(kVcaMode), switchV, 2, tip), 614, 45,  9, 24);
    addSlider(kVcaLevel, new KR106Slider(param(kVcaLevel), tip, sliderHdl), 634, 33, 19, 49);

    // === ENVELOPE SECTION ===
    addSlider(kEnvA, new KR106Slider(param(kEnvA), tip, sliderHdl), 656, 33, 20, 49)->setExtraRight(1);
    addSlider(kEnvD, new KR106Slider(param(kEnvD), tip, sliderHdl), 674, 33, 20, 49)->setExtraRight(1);
    addSlider(kEnvS, new KR106Slider(param(kEnvS), tip, sliderHdl), 692, 33, 20, 49)->setExtraRight(1);
    addSlider(kEnvR, new KR106Slider(param(kEnvR), tip, sliderHdl), 710, 33, 19, 49);

    // ADSR mode: horizontal 2-way (centered below envelope sliders)
    addML(kAdsrMode, new KR106HorizontalSwitch(param(kAdsrMode), switchH, 2, tip), 680, 84, 24, 9);

    // === CHORUS SECTION ===
    add(new KR106ChorusOff(param(kChorusI), param(kChorusII)), 735, 52, 17, 19);
    addML(kChorusI,  new KR106ButtonLED(param(kChorusI),  1, ledRed, param(kPower), tip), 751, 43, 17, 28);
    addML(kChorusII, new KR106ButtonLED(param(kChorusII), 2, ledRed, param(kPower), tip), 767, 43, 17, 28);

    // --- Deferred learnable registration for performance controls ---
    // Tab order: synth panel left-to-right, then performance section.
    auto regLearn = [&](int paramIdx, juce::Component* ctrl) {
        mLearnableParams.push_back(paramIdx);
        mLearnableControls.push_back({paramIdx, ctrl});
    };
    regLearn(kMasterVol, kMasterVolKnob);
    regLearn(kPortaRate, kPortaRateKnob);
    regLearn(kBenderDco, kBenderDcoSlider);
    regLearn(kBenderVcf, kBenderVcfSlider);
    regLearn(kBenderLfo, kBenderLfoSlider);
    regLearn(kPortaMode, kPortaModeSwitch);
    regLearn(kTuning, kTuningKnob);

    // === SCOPE ===
    mScope = add(new KR106Scope(&p), 790, 7, 128, 74);

    // === PRESET DISPLAY ===
    mPresetDisplay = add(new KR106PresetDisplay(&p), 790, 86, 128, 14);

    // === ICON BUTTONS (below chorus) ===
    {
        static const char* gearSvg =
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M10.325 4.317c.426 -1.756 2.924 -1.756 3.35 0a1.724 1.724 0 0 0 2.573 1.066c1.543 -.94 3.31 .826 2.37 2.37a1.724 1.724 0 0 0 1.065 2.572c1.756 .426 1.756 2.924 0 3.35a1.724 1.724 0 0 0 -1.066 2.573c.94 1.543 -.826 3.31 -2.37 2.37a1.724 1.724 0 0 0 -2.572 1.065c-.426 1.756 -2.924 1.756 -3.35 0a1.724 1.724 0 0 0 -2.573 -1.066c-1.543 .94 -3.31 -.826 -2.37 -2.37a1.724 1.724 0 0 0 -1.065 -2.572c-1.756 -.426 -1.756 -2.924 0 -3.35a1.724 1.724 0 0 0 1.066 -2.573c-.94 -1.543 .826 -3.31 2.37 -2.37c1 .608 2.296 .07 2.572 -1.065" /><path d="M9 12a3 3 0 1 0 6 0a3 3 0 0 0 -6 0" /></svg>)";

        static const char* dbSvg =
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6a8 3 0 1 0 16 0a8 3 0 1 0 -16 0" /><path d="M4 6v6a8 3 0 0 0 16 0v-6" /><path d="M4 12v6a8 3 0 0 0 16 0v-6" /></svg>)";

        add(new KR106IconButton(gearSvg, [this]() {
            showSettingsMenu();
        }), 741, 75, 18, 18);

        add(new KR106IconButton(dbSvg, [this]() {
            mPresetDisplay->openContextMenu();
        }), 760, 75, 18, 18);
    }

    // === KEYBOARD ===
    mKeyboard = add(new KR106Keyboard(&p, chevron), 129, 106, 792, 114);

#endif

    // Accept keyboard focus so UP/DN arrow keys cycle presets
    setWantsKeyboardFocus(true);

    mMenuTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::Segment14_otf, BinaryData::Segment14_otfSize);
    mScope->setTypeface(mMenuTypeface);

    // Tooltip overlay — added last so it paints on top of all controls
    mContent.addAndMakeVisible(mTooltip);
    mTooltip.setVisible(false);
    // Note: do NOT use setAlwaysOnTop(true) — it creates a separate native
    // peer that breaks mouse events in AU hosts on macOS Tahoe.
    // Use toFront() after addAndMakeVisible instead.

    // === SCALE & RESIZE SETUP ===
    // Order matters: we MUST set the correct initial size BEFORE calling
    // setResizable(), because setResizable() can trigger an early resized()
    // that derives scale from the editor's current width. If the width is
    // still the default 940 at that point, mUIScale gets clobbered to 1.0.
    //
    // Also: in Logic AU, the host queries the editor's size synchronously
    // right after construction (via JUCE's EditorCompHolder::getSizeToContainChild).
    // The AU wrapper has no parentSizeChanged → editor path, so any later
    // setSize calls won't propagate. The size we set here is what Logic uses
    // for the lifetime of the window.
    float savedScale = mProcessor.mUIScale > 0.f ? mProcessor.mUIScale : mUIScale;
    mUIScale = savedScale;

    mInternalResize = true;
#if KR106_TOUCH
    setSize(kBaseWidth, kBaseHeight);
#else
    setSize(juce::roundToInt(kBaseWidth * savedScale),
            juce::roundToInt(kBaseHeight * savedScale));
#endif
    mInternalResize = false;

    // Now enable host drag-resize. Lock aspect ratio so the UI scales uniformly.
    // Host provides resize handles for VST3/AU/LV2/CLAP/standalone.
    // Note: Logic AU drag-resize doesn't actually work due to a JUCE wrapper
    // limitation (parentSizeChanged → resizeHostWindow forces host back to
    // editor's bounds instead of forwarding new size). Users must use the
    // in-plugin Settings menu to change scale. This is the same limitation
    // Surge XT and other JUCE-based plugins have in Logic.
    setResizable(true, false);
#if KR106_TOUCH
    setResizeLimits(320, 160, 8192, 8192);   // any window shape; content is letterboxed
#else
    setResizeLimits(kBaseWidth, kBaseHeight, kBaseWidth * 4, kBaseHeight * 4);
    getConstrainer()->setFixedAspectRatio(
        static_cast<double>(kBaseWidth) / kBaseHeight);
#endif

    startTimerHz(30);
}

KR106Editor::~KR106Editor()
{
#if KR106_TOUCH
    mContent.removeMouseListener(&mLongPress);
    removeMouseListener(&mPanZoom);
    mKeysSheet.reset();
    mChoiceMenu.reset();
#endif
    stopTimer();
}

void KR106Editor::resized()
{
#if KR106_TOUCH
    // Fit the content into the window (letterboxed); zoom / pan are applied on top.
    updateTouchTransform();
    return;
#endif
    float scale;

    if (mInternalResize)
    {
        // We initiated this resize via applyScale — use our known scale
        scale = mUIScale;
        dbgResize("internal: " + juce::String(getWidth()) + "x" + juce::String(getHeight())
                + " scale=" + juce::String(scale, 2));
    }
    else
    {
        // Host-initiated resize (drag-resize, window management, etc.)
        // Derive scale from the new editor dimensions
        scale = static_cast<float>(getWidth()) / kBaseWidth;

        // Snap to nearest preset to avoid floating-point drift from
        // host rounding (Logic/AU can round pixel dimensions)
        for (float preset : { 1.f, 1.5f, 2.f, 3.f })
            if (std::abs(scale - preset) < 0.02f)
                scale = preset;

        // Only update local editor scale — don't write to processor or
        // settings. The processor's mUIScale is the source of truth,
        // set only by applyScale() (menu) or loadGlobalSettings().
        // This prevents the host's default 1x size from overwriting
        // the user's saved scale on editor reopen.
        mUIScale = scale;
        dbgResize("host: " + juce::String(getWidth()) + "x" + juce::String(getHeight())
                + " scale=" + juce::String(scale, 2));
    }

    mContent.setTransform(juce::AffineTransform::scale(scale));
    mContent.setBounds(0, 0, kBaseWidth, kBaseHeight);
}

void KR106Editor::updateTouchTransform()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    if (W <= 0.f || H <= 0.f) return;
    const float fit = std::min(W / (float) kBaseWidth, H / (float) kBaseHeight);
    const float sc = fit * mZoom;
    const float cw = kBaseWidth * sc, ch = kBaseHeight * sc;
    auto clampAxis = [](float t, float view, float content) {
        if (content <= view + 0.5f) return (view - content) * 0.5f;   // fits: centre it
        return juce::jlimit(view - content, 0.f, t);                   // zoomed: always cover the window
    };
    if (!mZoomInit) { mTx = (W - cw) * 0.5f; mTy = (H - ch) * 0.5f; mZoomInit = true; }
    mTx = clampAxis(mTx, W, cw);
    mTy = clampAxis(mTy, H, ch);
    mUIScale = mZoom;
    mContent.setTransform(juce::AffineTransform::scale(sc).translated(mTx, mTy));
    mContent.setBounds(0, 0, kBaseWidth, kBaseHeight);
    repaint();
}

// Zoom keeping the content point under 'focalPx' (window pixels) fixed.
void KR106Editor::setTouchZoom(float zoom, juce::Point<float> focalPx)
{
    zoom = juce::jlimit(1.f, 4.f, zoom);
    if (zoom < 1.03f) zoom = 1.f;
    const float W = (float) getWidth(), H = (float) getHeight();
    if (W <= 0.f || H <= 0.f) return;
    const float fit = std::min(W / (float) kBaseWidth, H / (float) kBaseHeight);
    const float oldSc = fit * mZoom, newSc = fit * zoom;
    if (!mZoomInit) updateTouchTransform();
    const float cx = (focalPx.x - mTx) / oldSc, cy = (focalPx.y - mTy) / oldSc;
    mZoom = zoom;
    mTx = focalPx.x - cx * newSc;
    mTy = focalPx.y - cy * newSc;
    updateTouchTransform();
}

void KR106Editor::applyScale(float s, bool resizeWindow)
{
#if KR106_TOUCH
    // Touch UI: the menu's 100/150/200/300% entries zoom the view (pan by dragging)
    juce::ignoreUnused(resizeWindow);
    setTouchZoom(s, { getWidth() * 0.5f, getHeight() * 0.5f });
    return;
#endif
    mUIScale = s;
    mProcessor.mUIScale = s;
    if (resizeWindow)
    {
        mInternalResize = true;
        setSize(juce::roundToInt(kBaseWidth * s),
                juce::roundToInt(kBaseHeight * s));
        mInternalResize = false;
    }
    else
    {
        // State restore: just update the transform, let host handle window size.
        mContent.setTransform(juce::AffineTransform::scale(s));
        mContent.setBounds(0, 0, kBaseWidth, kBaseHeight);
    }
}


void KR106Editor::mouseDown(const juce::MouseEvent& e)
{
    if (mClickDebugEnabled && mClickDebugger)
    {
        auto pos = e.getEventRelativeTo(this).getPosition();
        mLastClickPoint = pos;
        mClickDebugger->logDirect(pos);
        repaint();
    }

    // Cancel MIDI learn if active
    if (mProcessor.mMidiLearnParam.load(std::memory_order_relaxed) >= 0)
    {
        mProcessor.cancelMidiLearn();
        mTooltip.hide();
        return;
    }
    if (!e.mods.isPopupMenu()) return;
    showSettingsMenu();
}

void KR106Editor::showSettingsMenu()
{
    if (mSettingsMenu) return;

    int vc = mProcessor.mVoiceCount;
    std::vector<KR106MenuItem> items;
    // Left column
    items.push_back(KR106MenuItem::makeRadio(1,  "100%", mUIScale == 1.f));
    items.push_back(KR106MenuItem::makeRadio(2,  "150%", mUIScale == 1.5f));
    items.push_back(KR106MenuItem::makeRadio(3,  "200%", mUIScale == 2.f));
    items.push_back(KR106MenuItem::makeRadio(4,  "300%", mUIScale == 3.f));
    items.push_back(KR106MenuItem::sep());
    items.push_back(KR106MenuItem::makeRadio(10, "06 Voices",  vc == 6));
    items.push_back(KR106MenuItem::makeRadio(11, "08 Voices",  vc == 8));
    items.push_back(KR106MenuItem::makeRadio(12, "10 Voices", vc == 10));
    items.push_back(KR106MenuItem::sep());
    int os = mProcessor.mVcfOversample;
    items.push_back(KR106MenuItem::makeRadio(32, "VCF Oversample Off", os == 1));
    items.push_back(KR106MenuItem::makeRadio(30, "VCF Oversample 2x", os == 2));
    items.push_back(KR106MenuItem::makeRadio(31, "VCF Oversample 4x", os == 4));
    // Right column                                                    // index 10
    items.push_back(KR106MenuItem::item(20, "Ignore MIDI Velocity",      true, mProcessor.mIgnoreVelocity));
    items.push_back(KR106MenuItem::item(21, "Limit Arp to Kbd Range", true, mProcessor.mArpLimitKbd));
    items.push_back(KR106MenuItem::item(24, "Sync Arp to Host",      true, mProcessor.mArpSyncHost));
    items.push_back(KR106MenuItem::item(25, "Sync LFO to Host",      true, mProcessor.mLfoSyncHost));
    items.push_back(KR106MenuItem::item(22, "Mono Retrigger",        true, mProcessor.mMonoRetrigger));
    items.push_back(KR106MenuItem::item(26, "SEND MIDI SysEx",       true, mProcessor.mMidiOutSysEx));
    items.push_back(KR106MenuItem::sep());
    { auto* oscP = mProcessor.getParam(kSettingOscMode);
      bool blep = oscP && oscP->getValue() > 0.5f;
      items.push_back(KR106MenuItem::makeRadio(28, "PolyBLEP DCO", blep));
      items.push_back(KR106MenuItem::makeRadio(29, "Wavetable DCO", !blep)); }
    items.push_back(KR106MenuItem::sep());
    items.push_back(KR106MenuItem::makeAction(40, "Component Variance Editor"));
    items.push_back(KR106MenuItem::makeAction(41, "Keyboard Shortcuts"));
    if (kr106touch::audioMidiHook())
        items.push_back(KR106MenuItem::makeAction(43, "Options: State, Audio, MIDI"));
    // items.push_back(KR106MenuItem::item(42, "Click Debug Mode",          true, mClickDebugEnabled));

    mSettingsMenu = std::make_unique<KR106MenuSheet>(std::move(items), mMenuTypeface,
        [this](int r)
        {
            if (r == 0) { mSettingsMenu.reset(); return; }
            float s = r == 1 ? 1.f : r == 2 ? 1.5f : r == 3 ? 2.f : r == 4 ? 3.f : 0.f;
            if (s > 0.f && s != mUIScale)
                applyScale(s);
            // Voice count and oversample: set via parameter system
            int voices = r == 10 ? 6 : r == 11 ? 8 : r == 12 ? 10 : 0;
            if (voices > 0 && voices != mProcessor.mVoiceCount)
            {
                auto* p = mProcessor.getParam(kSettingVoices);
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(voices)));
            }
            int newOS = r == 32 ? 1 : r == 30 ? 2 : r == 31 ? 4 : 0;
            if (newOS > 0 && newOS != mProcessor.mVcfOversample)
            {
                auto* p = mProcessor.getParam(kSettingOversample);
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newOS)));
            }
            // Bool settings: toggle via parameter system
            auto toggleBoolParam = [this](int paramIdx) {
                auto* p = mProcessor.getParam(paramIdx);
                float cur = p->getValue();
                p->setValueNotifyingHost(cur > 0.5f ? 0.f : 1.f);
            };
            if (r == 20) toggleBoolParam(kSettingIgnoreVel);
            if (r == 21) toggleBoolParam(kSettingArpLimitKbd);
            if (r == 24) toggleBoolParam(kSettingArpSync);
            if (r == 25) toggleBoolParam(kSettingLfoSync);
            if (r == 22) toggleBoolParam(kSettingMonoRetrig);
            if (r == 26) toggleBoolParam(kSettingMidiSysEx);
            if (r == 28 || r == 29)
            {
                auto* p = mProcessor.getParam(kSettingOscMode);
                p->setValueNotifyingHost(r == 28 ? 1.f : 0.f);
            }
            if (r == 40)
            {
                mSettingsMenu.reset();
                showVarianceSheet();
                return;
            }
            if (r == 41)
            {
                mSettingsMenu.reset();
                showQwertyDiagram();
                return;
            }
            if (r == 43)
            {
                mSettingsMenu.reset();
                if (auto hook = kr106touch::audioMidiHook()) hook();
                return;
            }
            if (r == 42)
            {
                mClickDebugEnabled = !mClickDebugEnabled;
                if (mClickDebugEnabled && !mClickDebugger)
                {
                    mClickDebugger = std::make_unique<ClickDebugger>(*this, mLastClickPoint);
                    addMouseListener(mClickDebugger.get(), true);
                }
                else if (!mClickDebugEnabled)
                {
                    if (mClickDebugger)
                    {
                        removeMouseListener(mClickDebugger.get());
                        mClickDebugger.reset();
                    }
                    mLastClickPoint = { -1, -1 };
                    repaint();
                }
            }
            mProcessor.saveGlobalSettings();
        }, 12);

    int menuH = mSettingsMenu->calcHeight();
    int menuW = mSettingsMenu->calcWidth();
    int menuX = (kBaseWidth - menuW) / 2;
    int menuY = (kBaseHeight - menuH) / 2;

    mContent.addAndMakeVisible(mSettingsMenu.get());
    mSettingsMenu->showAt({ menuX, menuY, menuW, menuH });
}

void KR106Editor::showChoiceMenu(const juce::StringArray& names, int current,
                                 juce::Rectangle<int> anchor, std::function<void(int)> onPick)
{
    if (mChoiceMenu) return;
    std::vector<KR106MenuItem> items;
    for (int i = 0; i < names.size(); i++)
        items.push_back(KR106MenuItem::makeRadio(i + 1, names[i], i == current));

    mChoiceMenu = std::make_unique<KR106MenuSheet>(std::move(items), mMenuTypeface,
        [this, onPick](int r)
        {
            mChoiceMenu.reset();
            if (r > 0 && onPick) onPick(r - 1);
        });

    int menuW = mChoiceMenu->calcWidth(), menuH = mChoiceMenu->calcHeight();
    int menuX = juce::jlimit(0, juce::jmax(0, kBaseWidth - menuW), anchor.getX());
    int menuY = juce::jlimit(0, juce::jmax(0, kBaseHeight - menuH), anchor.getY() - menuH - 4);
    mContent.addAndMakeVisible(mChoiceMenu.get());
    mChoiceMenu->showAt({ menuX, menuY, menuW, menuH });
}

void KR106Editor::showVarianceSheet()
{
    if (mVarianceSheet) return;

    mVarianceSheet = std::make_unique<KR106VarianceSheet>(&mProcessor, mMenuTypeface,
        [this]()
        {
            mVarianceSheet.reset();
            mProcessor.saveGlobalSettings();
        });

    int sheetW = mVarianceSheet->calcWidth();
    int sheetH = mVarianceSheet->calcHeight();
    int sheetX = (kBaseWidth - sheetW) / 2;
    int sheetY = (kBaseHeight - sheetH) / 2;

    mContent.addAndMakeVisible(mVarianceSheet.get());
    mVarianceSheet->showAt({ sheetX, sheetY, sheetW, sheetH });
}

void KR106Editor::showQwertyDiagram()
{
    if (mQwertyDiagram) return;

    // Transparent overlay covers the whole window to catch clicks anywhere
    struct Overlay : public juce::Component {
        std::function<void()> onClose;
        KR106QwertyDiagram diagram;
        Overlay() { addAndMakeVisible(diagram); }
        void mouseDown(const juce::MouseEvent&) override { if (onClose) onClose(); }
        void resized() override {
            int w = 386, h = 120;
            diagram.setBounds((getWidth() - w) / 2, (getHeight() - h) / 2, w, h);
        }
    };

    auto* overlay = new Overlay();
    overlay->onClose = [this]() { mQwertyDiagram.reset(); };
    overlay->diagram.onClose = [this]() { mQwertyDiagram.reset(); };
    overlay->setBounds(0, 0, kBaseWidth, kBaseHeight);
    mContent.addAndMakeVisible(overlay);
    overlay->toFront(false);
    mQwertyDiagram.reset(overlay);
}

int KR106Editor::qwertyToNote(int keyCode) const
{
    // Standard piano layout: two octaves + extensions on all rows.
    // Lower octave (Z row naturals + A row sharps):
    //   Z=C  S=C#  X=D  D=D#  C=E  V=F  G=F#  B=G  H=G#  N=A  J=A#  M=B
    //   ,=C+1  L=C#+1  .=D+1  ;=D#+1  /=E+1  '=F+1
    // Upper octave (Q row naturals + number row sharps):
    //   Q=C  2=C#  W=D  3=D#  E=E  R=F  5=F#  T=G  6=G#  Y=A  7=A#  U=B
    //   I=C+1  9=C#+1  O=D+1  0=D#+1  P=E+1  [=F+1  ]=F#+1  \=G+1  ==G#+1
    switch (keyCode)
    {
        // Lower octave naturals (Z row)
        case 'Z': return mQwertyBase;
        case 'X': return mQwertyBase + 2;
        case 'C': return mQwertyBase + 4;
        case 'V': return mQwertyBase + 5;
        case 'B': return mQwertyBase + 7;
        case 'N': return mQwertyBase + 9;
        case 'M': return mQwertyBase + 11;
        case ',': return mQwertyBase + 12;
        case '.': return mQwertyBase + 14;
        case '/': return mQwertyBase + 16;
        // Lower octave sharps (A row)
        case 'S': return mQwertyBase + 1;
        case 'D': return mQwertyBase + 3;
        case 'G': return mQwertyBase + 6;
        case 'H': return mQwertyBase + 8;
        case 'J': return mQwertyBase + 10;
        case 'L': return mQwertyBase + 13;
        case ';': return mQwertyBase + 15;
        case '\'': return mQwertyBase + 17;
        // Upper octave naturals (Q row)
        case 'Q': return mQwertyBase + 12;
        case 'W': return mQwertyBase + 14;
        case 'E': return mQwertyBase + 16;
        case 'R': return mQwertyBase + 17;
        case 'T': return mQwertyBase + 19;
        case 'Y': return mQwertyBase + 21;
        case 'U': return mQwertyBase + 23;
        case 'I': return mQwertyBase + 24;
        case 'O': return mQwertyBase + 26;
        case 'P': return mQwertyBase + 28;
        case '[': return mQwertyBase + 29;
        case ']': return mQwertyBase + 31;
        // Upper octave sharps (number row)
        case '2': return mQwertyBase + 13;
        case '3': return mQwertyBase + 15;
        case '5': return mQwertyBase + 18;
        case '6': return mQwertyBase + 20;
        case '7': return mQwertyBase + 22;
        case '9': return mQwertyBase + 25;
        case '0': return mQwertyBase + 27;
        case '-': return mQwertyBase + 30;
        case '=': return mQwertyBase + 32;
        default:  return -1;
    }
}

void KR106Editor::qwertyAllNotesOff()
{
    for (int i = 0; i < 128; i++)
    {
        if (mQwertyDown[i])
        {
            mProcessor.sendMidiFromUI(0x80, static_cast<uint8_t>(i), 0);
            mKeyboard->setNoteFromMidi(i, false);
            mQwertyDown[i] = false;
        }
    }
}

void KR106Editor::advanceMidiLearn(int dir)
{
    int learnParam = mProcessor.mMidiLearnParam.load(std::memory_order_relaxed);
    if (learnParam < 0 || mLearnableControls.empty()) return;

    int idx = -1;
    for (int i = 0; i < (int)mLearnableControls.size(); i++)
    {
        if (mLearnableControls[i].paramIdx == learnParam) { idx = i; break; }
    }
    int n = (int)mLearnableControls.size();
    int next = (idx >= 0) ? (idx + dir + n) % n : 0;
    auto& lc = mLearnableControls[next];
    mProcessor.startMidiLearn(lc.paramIdx);

    // Show tooltip with "MIDI LEARN" on the target control
    auto* param = mProcessor.getParam(lc.paramIdx);
    int cc = mProcessor.getCCForParam(lc.paramIdx);
    mTooltip.setLine2(cc >= 0 ? "MIDI LEARN (CC " + juce::String(cc) + ")" : "MIDI LEARN");
    mTooltip.show(param, lc.ctrl);

    repaint();
}

bool KR106Editor::keyPressed(const juce::KeyPress& key)
{
    int code = key.getKeyCode();
    // Ignore case
    if (code >= 'a' && code <= 'z')
    {
        code -= 'a' - 'A';
    }

    // Tab / Shift+Tab / [ / ]: advance MIDI learn to next/prev control
    if (code == juce::KeyPress::tabKey || code == ']' || code == '[')
    {
        int learnParam = mProcessor.mMidiLearnParam.load(std::memory_order_relaxed);
        if (learnParam >= 0 && !mLearnableParams.empty())
        {
            int dir = (key.getModifiers().isShiftDown() || code == '[') ? -1 : 1;
            advanceMidiLearn(dir);
            return true;
        }
    }
    // Escape: cancel MIDI learn
    if (key == juce::KeyPress::escapeKey)
    {
        if (mProcessor.mMidiLearnParam.load(std::memory_order_relaxed) >= 0)
        {
            mProcessor.cancelMidiLearn();
            mTooltip.hide();
            repaint();
            return true;
        }
    }

    // Left/right arrows: cycle scope modes
    if (key == juce::KeyPress::leftKey)  { mScope->cycleMode(-1); return true; }
    if (key == juce::KeyPress::rightKey) { mScope->cycleMode(1);  return true; }

    // Up/down/page keys: preset navigation
    int delta = 0;
    if (key == juce::KeyPress::upKey)        delta = -1;
    else if (key == juce::KeyPress::downKey) delta =  1;
    else if (key == juce::KeyPress::pageUpKey)   delta = -8;
    else if (key == juce::KeyPress::pageDownKey) delta =  8;

    if (delta != 0)
    {
        int num = mProcessor.getNumPrograms();
        if (num <= 0) return true;
        int idx = mProcessor.getCurrentProgram() + delta;
        idx = ((idx % num) + num) % num;
        mProcessor.setCurrentProgram(idx);
        repaint();
        return true;
    }

    // Enter: open preset sheet
    if (key == juce::KeyPress::returnKey) { mPresetDisplay->openPresetSheet(); return true; }

    // `/1: octave shift
    if (code == '`' || code == 0x60) { mQwertyBase = juce::jmax(0, mQwertyBase - 12);  qwertyAllNotesOff(); return true; }
    if (code == '1')                 { mQwertyBase = juce::jmin(108, mQwertyBase + 12); qwertyAllNotesOff(); return true; }

    // QWERTY note keys — pass through if Cmd/Ctrl is held (e.g. Cmd+Q to quit)
    if (key.getModifiers().isCommandDown())
        return false;
    int note = qwertyToNote(code);
    if (note >= 0 && note <= 127)
    {
        if (!mQwertyDown[note])
        {
            bool holdOn = mProcessor.getParam(kHold)->getValue() > 0.5f;
            bool alreadyHeld = holdOn && mProcessor.mKeyboardHeld.test(note);

            if (alreadyHeld)
            {
                // Second press on held note: force release (same as mouse second-click)
                mProcessor.forceReleaseNote(note);
                mKeyboard->setNoteFromMidi(note, false);
            }
            else
            {
                mQwertyDown[note] = true;
                mProcessor.sendMidiFromUI(0x90, static_cast<uint8_t>(note), 127);
                mKeyboard->setNoteFromMidi(note, true);
            }
        }
        return true;  // consume key repeats too
    }

    return false;  // unhandled keys pass to DAW
}

bool KR106Editor::keyStateChanged(bool /*isKeyDown*/)
{
    // Check for released QWERTY keys.
    // Multiple keys can map to the same note (overlapping octaves),
    // so check all keys that produce this note and only release if
    // none of them are held.
    static constexpr int kNoteKeys[][3] = {
        // offset: up to 3 key codes that produce base+offset (0 = end)
        /* 0  C   */ {'Z', 0, 0},
        /* 1  C#  */ {'S', 0, 0},
        /* 2  D   */ {'X', 0, 0},
        /* 3  D#  */ {'D', 0, 0},
        /* 4  E   */ {'C', 0, 0},
        /* 5  F   */ {'V', 0, 0},
        /* 6  F#  */ {'G', 0, 0},
        /* 7  G   */ {'B', 0, 0},
        /* 8  G#  */ {'H', 0, 0},
        /* 9  A   */ {'N', 0, 0},
        /* 10 A#  */ {'J', 0, 0},
        /* 11 B   */ {'M', 0, 0},
        /* 12 C+1 */ {'Q', ',', 0},
        /* 13 C#+1*/ {'2', 'L', 0},
        /* 14 D+1 */ {'W', '.', 0},
        /* 15 D#+1*/ {'3', ';', 0},
        /* 16 E+1 */ {'E', '/', 0},
        /* 17 F+1 */ {'R', '\'', 0},
        /* 18 F#+1*/ {'5', 0, 0},
        /* 19 G+1 */ {'T', 0, 0},
        /* 20 G#+1*/ {'6', 0, 0},
        /* 21 A+1 */ {'Y', 0, 0},
        /* 22 A#+1*/ {'7', 0, 0},
        /* 23 B+1 */ {'U', 0, 0},
        /* 24 C+2 */ {'I', 0, 0},
        /* 25 C#+2*/ {'9', 0, 0},
        /* 26 D+2 */ {'O', 0, 0},
        /* 27 D#+2*/ {'0', 0, 0},
        /* 28 E+2 */ {'P', 0, 0},
        /* 29 F+2 */ {'[', 0, 0},
        /* 30 F#+2*/ {'-', 0, 0},
        /* 31 G+2 */ {']', 0, 0},
        /* 32 G#+2*/ {'=', 0, 0},
    };
    static constexpr int kMaxOffset = 32;

    bool handled = false;
    for (int i = 0; i < 128; i++)
    {
        if (!mQwertyDown[i]) continue;
        int offset = i - mQwertyBase;
        if (offset < 0 || offset > kMaxOffset) { mQwertyDown[i] = false; continue; }

        // Check if any key for this note is still held
        bool anyHeld = false;
        for (int k = 0; k < 3 && kNoteKeys[offset][k] != 0; k++)
            if (juce::KeyPress::isKeyCurrentlyDown(kNoteKeys[offset][k]))
                anyHeld = true;

        if (!anyHeld)
        {
            mQwertyDown[i] = false;
            mProcessor.sendMidiFromUI(0x80, static_cast<uint8_t>(i), 0);
            // Don't visually release if Hold is on — updateFromProcessor
            // keeps the key lit via mKeyboardHeld (same as mouse behavior)
            bool holdOn = mProcessor.getParam(kHold)->getValue() > 0.5f;
            if (!holdOn)
                mKeyboard->setNoteFromMidi(i, false);
            handled = true;
        }
    }
    return handled;
}

void KR106Editor::paintTouchBackground(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0c0c0c));
    g.addTransform(mContent.getTransform());
    g.setColour(juce::Colour(0xff434343));
    g.fillRect(0, 0, kBaseWidth, kBaseHeight);
    g.setColour(juce::Colour(0xff2e2e2e));                 // top bar
    g.fillRect(0, 0, kBaseWidth, 66);
    g.setColour(juce::Colour(0xffc60a0a));
    g.fillRect(0, 64, kBaseWidth, 2);

    const juce::Colour red(0xff9a0e0e), blue(0xff14388f);
    for (auto& t : mTouchSections)
    {
        g.setColour(juce::Colour(0xff383838));
        g.fillRoundedRectangle(t.r.toFloat(), 4.f);
        g.setColour(t.blue ? blue : red);                   // header bar (top corners rounded)
        juce::Path hdr;
        hdr.addRoundedRectangle((float) t.r.getX(), (float) t.r.getY(), (float) t.r.getWidth(), 20.f,
                                4.f, 4.f, true, true, false, false);
        g.fillPath(hdr);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.f));
        if (t.name.isNotEmpty())
            g.drawText(t.name, t.r.getX() + 8, t.r.getY(), t.r.getWidth() - 12, 20,
                       juce::Justification::centredLeft);
    }
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(11.f));
    for (auto& l : mTouchLabels)
        if (l.group == 0 || l.group == mDelayTab)
            g.drawText(l.text, juce::Rectangle<float>(l.cx - 34.f, (float) l.y - 12.f, 68.f, 14.f),
                   juce::Justification::centred);
}

void KR106Editor::setDelayTab(int tab)
{
    mDelayTab = tab;
    for (int g = 0; g < 2; g++)
        for (auto* c : mDelayTabCtrls[g]) c->setVisible(g + 1 == tab);
    if (mDelayTabs) mDelayTabs->setSelected(tab - 1);
    repaint();
}

void KR106Editor::showKeysSheet()
{
#if KR106_TOUCH
    if (mKeysSheet) return;
    auto grad = juce::ImageCache::getFromMemory(BinaryData::kr106_bender_gradient2x_png,
                                                BinaryData::kr106_bender_gradient2x_pngSize);
    auto* bender = new KR106Bender(mProcessor.getParam(kBender), grad, &mProcessor);
    mKeysSheet = std::make_unique<KR106TouchKeysSheet>(&mProcessor, bender);
    mKeysSheet->onClose = [this]() { mKeysSheet.reset(); };
    mContent.addAndMakeVisible(mKeysSheet.get());
    mKeysSheet->setBounds(0, 226, kBaseWidth, kBaseHeight - 226);
    mKeysSheet->toFront(false);
#endif
}

void KR106Editor::paint(juce::Graphics& g)
{
#if KR106_TOUCH
    paintTouchBackground(g);
    return;
#endif
    // Black fill for any uncovered area (when host allows non-aspect-ratio sizes)
    g.fillAll(juce::Colours::black);

    // Draw @2x background at the content-scaled size (matches mContent transform).
    // Using getWidth()/getHeight() here is wrong because the host may apply its
    // own DPI scaling (setScaleFactor) on top of our UI scale.
    float bgW = kBaseWidth * mUIScale;
    float bgH = kBaseHeight * mUIScale;
    g.drawImage(mBackground,
                0.f, 0.f, bgW, bgH,
                0, 0, mBackground.getWidth(), mBackground.getHeight());
}

void KR106Editor::timerCallback()
{
#if KR106_TOUCH
    if (mDDModeBtn) mDDModeBtn->refresh();
    if (mArpPatternBtn)
    {
        mArpPatternBtn->refresh();
        mArpShBtn->setEnabledLook(kr106::patternFromValue(mProcessor.getParam(kArpPattern)->getValue()) == kr106::kPatRandomSH);
        mArpShBtn->refresh();
    }
#endif

    bool active = mProcessor.getParam(kPower)->getValue() > 0.5f
                  && !mProcessor.isSuspended();

    // When inactive (power off or host suspended), skip GUI updates to save CPU
    if (!active)
    {
        if (mWasActive)
        {
            // One final repaint so LEDs go dark, scope goes black, keys release
            mWasActive = false;
            for (auto* ctrl : mControls)
                ctrl->repaint();
            startTimerHz(4); // Drop to 4 Hz — just polling for reactivation
        }
        return;
    }

    // Just became active — restore full refresh rate
    if (!mWasActive)
    {
        mWasActive = true;
        startTimerHz(30);
    }

    // Restore transpose chevron after state restore
    if (mNeedChevronRestore)
    {
        mNeedChevronRestore = false;
        auto* p = mProcessor.getParam(kTransposeOffset);
        if (p)
        {
            int offset = (int)p->convertFrom0to1(p->getValue());
            if (offset != 0)
                mKeyboard->setTransposeKeyFromOffset(offset);
        }
    }

    // Check for MIDI learn completion
    {
        int learnedCC = mProcessor.mMidiLearnResult.exchange(-1, std::memory_order_acquire);
        if (learnedCC >= 0)
        {
            mTooltip.setLine2("CC " + juce::String(learnedCC));
            mTooltip.update();
            mProcessor.saveGlobalSettings();
        }
    }

    // Swap rate slider params when sync state changes
    mArpRateSlider->updateSyncState();
    mLfoRateSlider->updateSyncState();

    // Update scope, keyboard, and clip LED from processor state
    mScope->updateFromProcessor();
    mKeyboard->updateFromProcessor();
    float peak = mProcessor.mPeakLevel.exchange(0.f, std::memory_order_relaxed);
    mClipLED->update(peak);

    // Repaint knobs/sliders/switches at ~7.5 Hz (every 4th tick) for host
    // automation sync — they don't change during normal MIDI playback.
    // Scope and keyboard repaint themselves from updateFromProcessor() above.
    if (++mRepaintDivider >= 4)
    {
        mRepaintDivider = 0;
        for (auto* ctrl : mControls)
            if (ctrl != mScope && ctrl != mKeyboard)
                ctrl->repaint();
        // Update tooltip if visible (CC changes update value externally)
        if (mTooltip.isVisible())
            mTooltip.update();
    }
}
