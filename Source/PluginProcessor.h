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
  kModFirst,   // modulation block: 2 LFOs, envelope, 12-slot matrix (global, not in presets)
  kNumParams = kModFirst + 59
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

  // Modulation matrix: sources (LFO 1, LFO 2, envelope) -> curated parameters
  void applyModulation(int nFrames, const juce::MidiBuffer& midi);
  kr106::ModLFO mModLfo[2];
  kr106::ModEnv mModEnv;
  uint32_t mSeenOns = 0, mSeenOffs = 0;
  std::atomic<float> mModSrcLast[4] {{0.f}, {0.f}, {0.f}, {0.f}};   // LFO 1, LFO 2, envelope: last values for the LEDs
  float getModSourceValue(int s) const { return (s >= 0 && s < 4) ? mModSrcLast[s].load(std::memory_order_relaxed) : 0.f; }
  float mModSent[kNumParams] = {};
  bool  mModActive[kNumParams] = {};

  void publishLatchedNotes();                       // audio thread
  std::atomic<uint32_t> mLatchVer{0};
  std::atomic<int> mLatchCount{0};
  std::atomic<uint8_t> mLatchNotes[128];
  std::atomic<bool> mLfoTriggered{false}; // CC1 mod state (audio→UI)

  // MIDI learn: param→CC map (multiple params can share a CC)
  int mParamCC[kNumParams];                 // param→CC (-1 = no user mapping)
  std::atomic<int> mMidiLearnParam{-1};     // param waiting for CC (-1 = inactive)
  std::atomic<int> mMidiLearnResult{-1};    // CC number captured (audio→UI)

  void startMidiLearn(int paramIdx);
  void cancelMidiLearn();
  void clearMidiLearn(int paramIdx);
  int getCCForParam(int paramIdx) const;    // returns CC or -1

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
