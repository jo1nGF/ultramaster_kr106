#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

// Modulation sources for the matrix: three LFOs and one envelope.
// Controls follow the reference screenshot (XLFO / XADSR):
//   LFO:  waveform, RETRIG, Rate (TIME), Delay, S/H (blend to sample&hold),
//         Smooth (SMTH), Phase, Amount (AMT). SYNC is host-tempo sync (not in this version).
//   ENV:  Attack, Decay, S-Start (sustain level), S-Time (time for the sustain to fall to 0),
//         Release, Amp (output scale), Velocity (touch sensitivity).
// All inputs are normalized 0..1 (the APVTS values). Outputs are bipolar (-1..1) for LFOs
// and unipolar (0..1) for the envelope.
namespace kr106 {

enum ModWave { kMwSine = 0, kMwTriangle, kMwSquare, kMwSawUp, kMwSawDown, kMwSmoothRnd, kMwSampleHold, kNumModWaves };
static constexpr const char* kModWaveNames[kNumModWaves] = {
  "Sine", "Triangle", "Square", "Saw up", "Saw down", "Smooth rnd", "S&H"
};
static inline int modWaveFromValue(float v)
{ return std::max(0, std::min(kNumModWaves - 1, static_cast<int>(std::round(v * (kNumModWaves - 1))))); }
static inline float valueFromModWave(int w) { return static_cast<float>(w) / (kNumModWaves - 1); }

class ModLFO
{
public:
  void Init(float sr, uint32_t seed) { mSR = sr; mRng = seed | 1u; mPhase = 0.f; mAge = 1e9f; mShCur = Rnd(); mShNext = Rnd(); }

  void Retrigger(float phaseOffset)  // note-on with RETRIG on
  {
    mPhase = phaseOffset; mAge = 0.f; mShCur = mShNext; mShNext = Rnd();
  }

  // Advance by one block and return the output for that block.
  // p = normalized params: wave, rate, amt, delay, shBlend, smooth, phase.
  float Process(int nFrames, float wave, float rate, float amt, float delay, float shBlend, float smooth, float phase)
  {
    const int w = modWaveFromValue(wave);
    const float hz = 0.05f * std::pow(400.f, rate);               // 0.05 .. 20 Hz
    const float dt = static_cast<float>(nFrames) / mSR;

    // phase: the block start reads at the offset phase; phase advances by hz * dt
    const float ph = std::fmod(mPhase + phase, 1.f);
    float v;
    switch (w)
    {
      case kMwSine:       v = std::sin(6.2831853f * ph); break;
      case kMwTriangle:   v = 2.f * std::fabs(2.f * ph - 1.f) - 1.f; break;
      case kMwSquare:     v = ph < 0.5f ? 1.f : -1.f; break;
      case kMwSawUp:      v = 2.f * ph - 1.f; break;
      case kMwSawDown:    v = 1.f - 2.f * ph; break;
      case kMwSmoothRnd:  { float s = ph * ph * (3.f - 2.f * ph); v = mShCur + (mShNext - mShCur) * s; } break;
      default:            v = mShCur; break;                            // S&H
    }
    // S/H blend: slider 0 = chosen waveform, 1 = pure sample & hold
    const float sh = mShCur;
    v = v + (sh - v) * Clamp01(shBlend);

    // smoothing (one-pole, up to 200 ms)
    const float tau = 0.2f * smooth * smooth;
    const float a = tau < 1e-4f ? 1.f : 1.f - std::exp(-dt / tau);
    mSmooth += (v - mSmooth) * a;

    // delay fade-in
    const float dly = 5.f * delay * delay;
    const float fade = dly < 1e-4f ? 1.f : std::min(1.f, mAge / dly);

    // advance
    const float prev = mPhase;
    mPhase = std::fmod(mPhase + hz * dt, 1.f);
    if (mPhase < prev) { mShCur = mShNext; mShNext = Rnd(); }        // new cycle: new random value
    mAge += dt;

    return mSmooth * fade * amt;
  }

private:
  static float Clamp01(float x) { return std::min(1.f, std::max(0.f, x)); }
  float Rnd() { mRng = mRng * 1664525u + 1013904223u; return static_cast<float>(mRng >> 8) * (2.f / 16777216.f) - 1.f; }
  float mSR = 44100.f, mPhase = 0.f, mAge = 0.f, mSmooth = 0.f, mShCur = 0.f, mShNext = 0.f;
  uint32_t mRng = 1u;
};

class ModEnv
{
public:
  void Init(float sr) { mSR = sr; }

  void NoteOn(float velocity01) { mVel = Clamp01(velocity01); mStage = kAttack; mAttackFrom = mLevel; mT = 0.f; }
  void NoteOff() { if (mStage != kIdle) { mStage = kRelease; mRelFrom = mLevel; mT = 0.f; } }

  // Returns the envelope value for this block (0..1, already scaled by Amp and Velocity).
  // Stage times in seconds (shared with the on-screen envelope display)
  static void Times(float att, float dec, float sTime, float rel, float& tA, float& tD, float& tS, float& tR)
  {
    tA = 0.001f + 5.f * att * att;
    tD = 0.001f + 5.f * dec * dec;
    tS = 0.01f + 10.f * sTime * sTime;
    tR = 0.001f + 10.f * rel * rel;
  }

  // Envelope level (0..1, before Amp / Velocity) t seconds after note-on when the key is released
  // at time tGate. Same maths as Process(), used to draw the envelope.
  static float CurveAt(float t, float tGate, float tA, float tD, float tS, float tR, float sStart)
  {
    const float lvl = Clamp01(sStart);
    auto held = [&](float x) {
      if (x < tA) return x / tA;
      if (x < tA + tD) return lvl + (1.f - lvl) * std::exp(-(x - tA) / (tD * 0.25f));
      return lvl * std::max(0.f, 1.f - (x - tA - tD) / tS);
    };
    if (t < tGate) return held(t);
    return held(tGate) * std::max(0.f, 1.f - (t - tGate) / tR);
  }

  // Current state, for the display: 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
  int   StageIndex() const { return static_cast<int>(mStage); }
  float StageTime()  const { return mT; }
  float RawLevel()   const { return mLevel; }

  // Returns the envelope value for this block (0..1, already scaled by Amp and Velocity).
  float Process(int nFrames, float att, float dec, float sStart, float sTime, float rel, float amp, float velSens)
  {
    const float dt = static_cast<float>(nFrames) / mSR;
    float tA, tD, tS, tR;
    Times(att, dec, sTime, rel, tA, tD, tS, tR);
    const float lvl = Clamp01(sStart);
    switch (mStage)
    {
      case kAttack:
        mT += dt;
        mLevel = mAttackFrom + (1.f - mAttackFrom) * std::min(1.f, mT / tA);
        if (mT >= tA) { mStage = kDecay; mT = 0.f; mLevel = 1.f; }
        break;
      case kDecay:
        mT += dt;
        mLevel = lvl + (1.f - lvl) * std::exp(-mT / (tD * 0.25f));
        if (mT >= tD) { mStage = kSustain; mT = 0.f; mLevel = lvl; }
        break;
      case kSustain:
        mT += dt;                                                   // S-Time: slope from S-Start down to 0
        mLevel = lvl * std::max(0.f, 1.f - mT / tS);
        break;
      case kRelease:
        mT += dt;
        mLevel = mRelFrom * std::max(0.f, 1.f - mT / tR);
        if (mT >= tR) { mStage = kIdle; mLevel = 0.f; }
        break;
      default:
        mLevel = 0.f;
        break;
    }
    const float vel = (1.f - velSens) + velSens * mVel;
    return Clamp01(mLevel * Clamp01(amp) * vel);
  }

  bool IsIdle() const { return mStage == kIdle; }

private:
  enum Stage { kIdle, kAttack, kDecay, kSustain, kRelease };
  static float Clamp01(float x) { return std::min(1.f, std::max(0.f, x)); }
  float mSR = 44100.f, mLevel = 0.f, mT = 0.f, mRelFrom = 0.f, mAttackFrom = 0.f, mVel = 1.f;
  Stage mStage = kIdle;
};


// ---- matrix: sources and curated targets (choices are stored as 0..1 slider values) ----
// Source order: None, LFO 1, LFO 2, LFO 3, Envelope, Gate 1 ... Gate 8 (the gate tracks of the Seq page).
// (Saved states from before LFO 3 existed stored Envelope as index 3: kOldSourceToNew converts them.)
static constexpr int kNumModSources = 13;
static constexpr int kSrcNone = 0, kSrcLfo1 = 1, kSrcLfo2 = 2, kSrcLfo3 = 3, kSrcEnv = 4, kSrcGate1 = 5;
static constexpr const char* kModSourceNames[kNumModSources] = {
  "None", "LFO 1", "LFO 2", "LFO 3", "Envelope",
  "Gate 1", "Gate 2", "Gate 3", "Gate 4", "Gate 5", "Gate 6", "Gate 7", "Gate 8" };
static inline int modSourceFromValue(float v)
{ return std::max(0, std::min(kNumModSources - 1, static_cast<int>(std::round(v * (kNumModSources - 1))))); }
static inline float valueFromModSource(int s) { return static_cast<float>(s) / (kNumModSources - 1); }

// Targets (the last two, Arp Scale and Arp Root, move the scale quantizer). Besides the synth and effects, every control of the three LFOs and of the modulation
// envelope can be modulated, and so can portamento. (A modulated Mod-page control takes effect from
// the next audio block, so one LFO can drive another - or itself - without any trouble.)
static constexpr int kNumModTargets = 67;
static constexpr const char* kModTargetNames[kNumModTargets] = {
  // Filter & amp
  "VCF Freq", "VCF Res", "VCF Env", "VCF LFO", "HPF Freq", "VCA Level",
  // Oscillator & LFO
  "DCO LFO", "DCO PWM", "DCO Sub", "DCO Noise", "Tuning", "Portamento", "LFO Rate",
  // Envelope
  "Env Attack", "Env Decay", "Env Sustain", "Env Release",
  // Mod LFO 1 / 2 / 3
  "L1 Rate", "L1 Amount", "L1 Delay", "L1 S/H", "L1 Smooth", "L1 Phase",
  "L2 Rate", "L2 Amount", "L2 Delay", "L2 S/H", "L2 Smooth", "L2 Phase",
  "L3 Rate", "L3 Amount", "L3 Delay", "L3 S/H", "L3 Smooth", "L3 Phase",
  // Mod envelope
  "ME Attack", "ME Decay", "ME S-Start", "ME S-Time", "ME Release", "ME Amp", "ME Velocity",
  // Effects
  "Tape Time", "Tape Feedback", "Tape Mix", "Tape Tone", "Tape Wow",
  "Digital Time", "Digital Feedback", "Digital Mix",
  "Clouds Position", "Clouds Size", "Clouds Pitch", "Clouds Density", "Clouds Mix", "Clouds Reverb", "Clouds Decay", "Clouds Tone",
  // Arp & limiter
  "Arp Rate", "Arp Swing", "Arp Gate", "Arp Ratchet", "Arp Strum", "Arp Probability", "Limiter Ceiling",
  "Arp Scale", "Arp Root"
};
static constexpr int kNumModGroups = 11;
static constexpr const char* kModGroupNames[kNumModGroups] = {
  "Filter & amp", "Oscillator & LFO", "Envelope", "Mod LFO 1", "Mod LFO 2", "Mod LFO 3", "Mod envelope",
  "Tape delay", "Digital delay", "Clouds", "Arp, scale & limiter"
};
static constexpr int kModGroupStart[kNumModGroups] = { 0, 6, 13, 17, 23, 29, 35, 42, 47, 50, 58 };
static constexpr int kModGroupCount[kNumModGroups] = { 6, 7,  4,  6,  6,  6,  7,  5,  3,  8,  9 };
static inline int modGroupOf(int t)
{ for (int g = 0; g < kNumModGroups; g++) if (t >= kModGroupStart[g] && t < kModGroupStart[g] + kModGroupCount[g]) return g; return 0; }
static inline float valueFromModTarget(int t) { return static_cast<float>(t) / (kNumModTargets - 1); }
static inline int modTargetFromValue(float v)
{ return std::max(0, std::min(kNumModTargets - 1, static_cast<int>(std::round(v * (kNumModTargets - 1))))); }

// ---- saved states from before LFO 3 / the longer target list ----
// Old encoding: source = index / 3 (4 sources), target = index / 36 (37 targets).
static constexpr int kOldNumSources = 4, kOldNumTargets = 37;
static constexpr int kOldSourceToNew[kOldNumSources] = { kSrcNone, kSrcLfo1, kSrcLfo2, kSrcEnv };
static constexpr int kOldTargetToNew[kOldNumTargets] = {
   0,  1,  2,  3,  4,  5,            // VCF Freq .. VCA Level
   6,  7,  8,  9, 10, 12,            // DCO LFO .. Tuning, LFO Rate (Portamento was inserted before LFO Rate)
  13, 14, 15, 16,                    // Env A D S R
  42, 43, 44, 45, 46,                // Tape delay
  47, 48, 49,                        // Digital delay
  50, 51, 52, 53, 54, 55, 56, 57,    // Clouds
  58, 59, 60, 61, 64                 // Arp rate, swing, gate, ratchet, limiter ceiling (Strum / Probability were inserted)
};

} // namespace kr106
