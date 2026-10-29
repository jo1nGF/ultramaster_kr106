#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/KR106ModEngine.h"
#include <atomic>
#include <bitset>
#include "DSP/KR106_DSP.h"
#include "DSP/KR106SysEx.h"
#include "KR106PresetManager.h"

// Parameter indices — must match KR106DSP::SetParam's internal enum
enum EParams
{
  kBenderDco = 0, kBenderVcf, kArpRate, kLfoRate, kLfoDelay,
  kDcoLfo, kDcoPwm, kDcoSub, kDcoNoise, kHpfFreq,
  kVcfFreq, kVcfRes, kVcfEnv, kVcfLfo, kVcfKbd,
  kVcaLevel, kEnvA, kEnvD, kEnvS, kEnvR,
  kTranspose, kHold, kArpeggio, kDcoPulse, kDcoSaw, kDcoSubSw,
  kChorusOff, kChorusI, kChorusII,
  kOctTranspose, kArpMode, kArpRange, kLfoMode, kPwmMode,
  kVcfEnvInv, kVcaMode,
  kBender, kTuning, kPower,
  kPortaMode, kPortaRate,
  kTransposeOffset, kBenderLfo,
  kAdsrMode,
  kMasterVol,
  // Settings (exposed as params for headless LV2 hosts)
  kSettingVoices,       // 6, 8, or 10
  kSettingOversample,   // 2 or 4
  kSettingIgnoreVel,    // bool
  kSettingArpLimitKbd,  // bool
  kSettingArpSync,      // bool
  kSettingLfoSync,      // bool
  kSettingMonoRetrig,   // bool
  kSettingMidiSysEx,    // bool
  kArpQuantize,         // 0-8 note division (separate from kArpRate for headless/DSP use)
  kLfoQuantize,         // 0-12 note division (separate from kLfoRate for headless/DSP use)
  kSettingOscMode,      // 0 = wavetable+upsample, 1 = polyBLEP at Nx
  // Tape delay (appended last so older states/presets keep their indices)
  kDelayTime, kDelayFeedback, kDelayMix, kDelayTone, kDelayWow,
  // Added later (also appended, same reason): wow speed, arpeggiator patterns
  kDelayWowRate, kArpPattern, kArpShLoop,
  // Digital delay (clean stereo echo, after the tape delay)
  kDDelayTime, kDDelayFeedback, kDDelayMix, kDDelayTone, kDDelayLowCut, kDDelayMode,
  // Patch extras: LFO waveform and looping envelope (saved with presets)
  kLfoWave, kEnvLoop,
  // Arpeggiator feel + pattern automation (live params, not in presets)
  kArpSwing, kArpGate, kArpRatchet, kArpChord, kArpAutoMode, kArpAutoEvery, kArpAutoMask,
  // Master limiter
  kLimOn, kLimCeil, kLimRel,
  // Clouds (granular)
  kCldPos, kCldSize, kCldPitch, kCldDens, kCldTex, kCldSpread, kCldFb, kCldRev, kCldMix, kCldFreeze,
  kModFirst,   // modulation block: LFOs, envelope, 12-slot matrix (global, not in presets)
  kNumParams = kModFirst + 75
};

// Offsets into the modulation block (see KR106ModEngine.h for what each control does)
constexpr int kLfo1Wave = kModFirst + 0, kLfo1Rate = kModFirst + 1, kLfo1Amt = kModFirst + 2,
              kLfo1Delay = kModFirst + 3, kLfo1Retrig = kModFirst + 4, kLfo1SH = kModFirst + 5,
              kLfo1Smooth = kModFirst + 6, kLfo1Phase = kModFirst + 7;
constexpr int kLfo2Wave = kModFirst + 8, kLfo2Rate = kModFirst + 9, kLfo2Amt = kModFirst + 10,
              kLfo2Delay = kModFirst + 11, kLfo2Retrig = kModFirst + 12, kLfo2SH = kModFirst + 13,
              kLfo2Smooth = kModFirst + 14, kLfo2Phase = kModFirst + 15;
constexpr int kModEnvAtt = kModFirst + 16, kModEnvDec = kModFirst + 17, kModEnvSStart = kModFirst + 18,
              kModEnvSTime = kModFirst + 19, kModEnvRel = kModFirst + 20, kModEnvAmp = kModFirst + 21,
              kModEnvVel = kModFirst + 22;
constexpr int kModRouteBase = kModFirst + 23;     // 12 rows x (source, target, amount)
constexpr int kModRows = 12;
constexpr int kCldDecay = kModFirst + 59, kCldTone = kModFirst + 60;   // Clouds reverb decay (0.5..30 s) and tone
constexpr int kCldMode = kModFirst + 61;                              // Clouds firmware (0 granular, 1 pitch shift, 2 looping delay, 3 spectral)

// Appended later (indices of older states stay valid)
constexpr int kArpStrum = kModFirst + 62;     // chord strum: 0 = together .. 1 = spread over the whole step
constexpr int kArpProb  = kModFirst + 63;     // arp trigger probability (default 100 %)
constexpr int kScaleType = kModFirst + 64;    // scale quantizer: 0 = off ... custom (see KR106Scale.h)
constexpr int kScaleRoot = kModFirst + 65;    // C .. B
constexpr int kScaleMask = kModFirst + 66;    // custom scale: 12-bit mask of allowed pitch classes
constexpr int kLfo3Wave = kModFirst + 67, kLfo3Rate = kModFirst + 68, kLfo3Amt = kModFirst + 69,
              kLfo3Delay = kModFirst + 70, kLfo3Retrig = kModFirst + 71, kLfo3SH = kModFirst + 72,
              kLfo3Smooth = kModFirst + 73, kLfo3Phase = kModFirst + 74;
constexpr int kNumModLfos = 3;
constexpr int kModFormat = 5;    // version of the matrix Source / Target encoding written to the state

class KR106AudioProcessor : public juce::AudioProcessor
{
public:
  KR106AudioProcessor();
  ~KR106AudioProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "KR-106"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  // Set active patch slot without loading patch
  void setCurrentProgramIndex(int index);
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // Thread-safe queues for UI↔audio communication
  void forceReleaseNote(int noteNum);

  // Notes latched by Hold (with or without the arpeggiator). The audio thread
  // publishes them after every block so getStateInformation() can save them;
  // setStateInformation() puts them back. (seqlock: lock-free for the audio thread)
  std::vector<int> readLatchedNotes() const;
  void restoreLatchedNotes(const std::vector<int>& notes);
  void sendMidiFromUI(uint8_t status, uint8_t data1, uint8_t data2);

  // Fast param access for UI controls
  juce::RangedAudioParameter* getParam(int idx) { return mParams[idx]; }

  KR106DSP<float> mDSP;
  KR106PresetManager mPresetMgr;
  float mUIScale = 0.f;  // 0 = auto-detect (default), otherwise 1.0/1.5/2.0
  int mVoiceCount = 6;   // persisted per instance (6/8/10)
  bool mIgnoreVelocity = true; // persisted per instance
  bool mArpLimitKbd = true;    // persisted per instance
  bool mArpSyncHost = false;   // persisted per instance (sync arp to DAW tempo)
  bool mLfoSyncHost = false;   // persisted per instance (sync LFO to DAW tempo)
  bool mMonoRetrigger = true;  // persisted per instance
  bool mMidiOutSysEx = false;  // false=CC/PC output, true=SysEx output (J106 style)

  // Cached host transport state (for LV2 hosts that only send updates on change)
  bool mCachedPlaying = false;
  double mCachedBPM = 120.0;
  double mCachedPPQ = 0.0;
  bool mCachedHaveBPM = false;
  bool mCachedHavePPQ = false;

  int mVcfOversample = 4;      // persisted per instance (2 or 4)
  bool mInitialDefault = true; // shows "Default" until first preset change

  // Preset CSV operations
  void reloadPresetsFromFile(const juce::File& file);
  void saveCurrentPresetToCSV(const juce::String& name);
  void renameCurrentPreset(const juce::String& name);
  void clearCurrentPreset();
  void pastePreset(const KR106Preset& preset);
  KR106Preset getPreset(int idx) const;
  bool isCurrentPresetDirty() const;
  juce::File getPresetCSVPath() const { return KR106PresetManager::getDefaultCSVPath(); }

  // Persist global settings (right-click menu: zoom, voices, etc.)
  void saveGlobalSettings();
  void loadGlobalSettings();

  // Clip indicator: peak level before saturator (audio writes, UI reads)
  std::atomic<float> mPeakLevel{0.f};

  // Scope page (UI reads/writes, saved in DAW state)
  int mScopePage = 0;  // 0-5: waveform, spectrum, ADSR, VCF, patch bank, about

  // Scope ring buffer (audio writes, UI reads via timer)
  static constexpr int kScopeRingSize = 4096;
  float mScopeRing[kScopeRingSize] = {};    // L channel
  float mScopeRingR[kScopeRingSize] = {};   // R channel
  float mScopeSyncRing[kScopeRingSize] = {};
  std::atomic<int> mScopeWritePos{0};

  // Keyboard held-note display (audio thread writes, UI timer reads)
  std::bitset<128> mKeyboardHeld;

  // Modulation matrix: sources (LFO 1-3, envelope) -> curated parameters
  void applyModulation(int nFrames, const juce::MidiBuffer& midi);
  kr106::ModLFO mModLfo[kNumModLfos];
  kr106::ModEnv mModEnv;
  uint32_t mSeenOns = 0, mSeenOffs = 0;
  std::atomic<float> mModSrcLast[kr106::kNumModSources] { {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f}, {0.f} };   // LFO 1-3, envelope, seq 1-8, trk CV 1-2: last values
  // Value a control should show: the modulated value while the matrix drives it, else the user value
  float modDisplayValue(int p) const { return (p >= 0 && p < kNumParams && mModActive[p]) ? mModSent[p] : (mParams[p] ? mParams[p]->getValue() : 0.f); }
  kr106::VarigateSeq& getSeq() { return mDSP.mSeq; }       // the step sequencers (Seq page)
  kr106::MetropolixSeq& getMetro() { return mDSP.mMetro; }
  int getSeqMode() const { return mDSP.mSeqMode.load(); }  // 0 = Varigate, 1 = Metropolix
  void setSeqMode(int m)                                    // the one that was playing stops
  {
    m = m ? 1 : 0;
    if (m == mDSP.mSeqMode.load()) return;
    mDSP.mSeq.mRun.store(false); mDSP.mMetro.mRun.store(false);
    mDSP.mSeqMode.store(m);
  }

  // Modulation envelope state for the on-screen display (stage 0 idle .. 4 release, seconds in stage, raw level)
  std::atomic<int>   mModEnvStage { 0 };
  std::atomic<float> mModEnvT { 0.f }, mModEnvLevel { 0.f };
  float getModSourceValue(int s) const { return (s >= 0 && s < kr106::kNumModSources) ? mModSrcLast[s].load(std::memory_order_relaxed) : 0.f; }
  float mModSent[kNumParams] = {};
  bool  mModActive[kNumParams] = {};

  void publishLatchedNotes();                       // audio thread
  std::atomic<uint32_t> mLatchVer{0};
  std::atomic<int> mLatchCount{0};
  std::atomic<uint8_t> mLatchNotes[128];
  std::atomic<bool> mLfoTriggered{false}; // CC1 mod state (audio→UI)

  // MIDI learn: param→CC map (multiple params can share a CC)
  static constexpr int kCCDisabled = -2;
  int mParamCC[kNumParams];                 // param→CC (-1 = no user mapping: factory CC, -2 = no CC at all)
  std::atomic<int> mMidiLearnParam{-1};     // param waiting for CC (-1 = inactive)
  std::atomic<int> mMidiLearnResult{-1};    // CC number captured (audio→UI)

  void startMidiLearn(int paramIdx);
  void cancelMidiLearn();
  // Remove the CC the user assigned to this control: the factory CC (if the control has one) applies again
  void clearMidiLearn(int paramIdx);      // = back to the default (factory) CC
  // Take the CC away from this control altogether, even the factory one (it answers to no CC until
  // the user learns a new one or goes back to the default)
  void disableMidiCC(int paramIdx);
  bool isMidiCCDisabled(int paramIdx) const { return paramIdx >= 0 && paramIdx < kNumParams && mParamCC[paramIdx] == kCCDisabled; }
  int getCCForParam(int paramIdx) const;    // returns CC or -1 (user assignment first, then the factory one)
  int getDefaultCCForParam(int paramIdx) const;   // factory CC of this control or -1
  bool hasUserCC(int paramIdx) const { return paramIdx >= 0 && paramIdx < kNumParams && mParamCC[paramIdx] >= 0; }

private:
  // Parameter listener: push param changes to DSP
  void parameterChanged(int paramIdx, float newValue);

  // Fast access to JUCE params by index
  juce::RangedAudioParameter* mParams[kNumParams] = {};

  // Get denormalized param value (0-1 for sliders, int range for switches, etc.)
  float getParamValue(int idx) const
  {
    return mParams[idx]->convertFrom0to1(mParams[idx]->getValue());
  }

  // Last-applied values — compared in processBlock to detect changes
  float mLastParamValues[kNumParams] = {};
  bool mPowerOn = true;
  bool mWasJ106Mode = true; // tracks ADSR mode for HPF/VCF remap detection

  // Fade-in/out to prevent clicks on audio stream open/close
  static constexpr int mFadeInTotal = 64; // samples (~1.5ms at 44.1k)
  int mFadeInRemaining = 0;
  std::atomic<bool> mShouldFadeOut{false};
  int mFadeOutRemaining = 0;
  static constexpr int mFadeOutTotal = 64;

  // Called before standalone shutdown to ramp output to zero
  void requestFadeOut() { mShouldFadeOut.store(true, std::memory_order_release); }

  // UI→audio thread-safe note release queue
  static constexpr int kForceReleaseQueueSize = 64;
  int mForceReleaseQueue[kForceReleaseQueueSize] = {};
  std::atomic<int> mForceReleaseHead{0};
  std::atomic<int> mForceReleaseTail{0};
  std::atomic<bool> mHoldOff{false};

  // UI→audio MIDI queue (keyboard notes, LFO trigger CC)
  struct UIMidiEvent { uint8_t status, data1, data2; };
  static constexpr int kUIMidiQueueSize = 256;
  UIMidiEvent mUIMidiQueue[kUIMidiQueueSize] = {};
  std::atomic<int> mUIMidiHead{0};
  std::atomic<int> mUIMidiTail{0};

  int mCurrentPreset = 0;  // 0-127 within current bank
  int presetBankOffset() const { return (mDSP.mSynthModel == kr106::kJ106) ? 128 : 0; }
  int absPresetIndex() const { return mCurrentPreset + presetBankOffset(); }

  // Flag: emit SysEx dump for current preset values on next processBlock
  std::atomic<bool> mSendPresetSysEx{false};
  bool mPresetClean = false; // true after preset load, cleared on first param edit

  // SysEx switch byte decode helpers (shared by IPR and APR receive)

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KR106AudioProcessor)
};
