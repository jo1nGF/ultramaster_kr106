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
  float Process(int nFrames, float att, float dec, float sStart, float sTime, float rel, float amp, float velSens)
  {
    const float dt = static_cast<float>(nFrames) / mSR;
    const float tA = 0.001f + 5.f * att * att;
    const float tD = 0.001f + 5.f * dec * dec;
    const float tS = 0.01f + 10.f * sTime * sTime;
    const float tR = 0.001f + 10.f * rel * rel;
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
static constexpr int kNumModSources = 5;
static constexpr const char* kModSourceNames[kNumModSources] = { "None", "LFO 1", "LFO 2", "LFO 3", "Envelope" };
// Preserve the old encoding anchors: None=0, LFO 1=1/3, LFO 2=2/3,
// Envelope=1. LFO 3 occupies a new anchor at 5/6 so old saved routes remain valid.
static inline float valueFromModSource(int s)
{
  static constexpr float values[kNumModSources] = { 0.f, 1.f / 3.f, 2.f / 3.f, 5.f / 6.f, 1.f };
  return values[std::max(0, std::min(kNumModSources - 1, s))];
}
static inline int modSourceFromValue(float v)
{
  v = std::max(0.f, std::min(1.f, v));
  if (v < 1.f / 6.f) return 0;
  if (v < 0.5f) return 1;
  if (v < 0.75f) return 2;
  if (v < 11.f / 12.f) return 3;
  return 4;
}

static constexpr int kNumModTargets = 69;
// Grouped so the target menu is two short menus (group, then item)
static constexpr const char* kModTargetNames[kNumModTargets] = {
  "VCF Freq", "VCF Res", "VCF Env", "VCF LFO", "HPF Freq", "VCA Level",
  "DCO LFO", "DCO PWM", "DCO Sub", "DCO Noise", "Tuning", "LFO Rate",
  "Env Attack", "Env Decay", "Env Sustain", "Env Release",
  "Tape Time", "Tape Feedback", "Tape Mix", "Tape Tone", "Tape Wow",
  "Digital Time", "Digital Feedback", "Digital Mix",
  "Clouds Position", "Clouds Size", "Clouds Pitch", "Clouds Density", "Clouds Mix", "Clouds Reverb", "Clouds Decay", "Clouds Tone",
  "Arp Rate", "Arp Swing", "Arp Gate", "Arp Ratchet", "Limiter Ceiling",
  "LFO 1 Wave", "LFO 1 Rate", "LFO 1 Amount", "LFO 1 Delay", "LFO 1 Retrig", "LFO 1 S/H", "LFO 1 Smooth", "LFO 1 Phase",
  "LFO 2 Wave", "LFO 2 Rate", "LFO 2 Amount", "LFO 2 Delay", "LFO 2 Retrig", "LFO 2 S/H", "LFO 2 Smooth", "LFO 2 Phase",
  "LFO 3 Wave", "LFO 3 Rate", "LFO 3 Amount", "LFO 3 Delay", "LFO 3 Retrig", "LFO 3 S/H", "LFO 3 Smooth", "LFO 3 Phase",
  "Mod Env Attack", "Mod Env Decay", "Mod Env S-Start", "Mod Env S-Time", "Mod Env Release", "Mod Env Amp", "Mod Env Velocity",
  "Portamento Rate"
};
static constexpr int kNumModGroups = 12;
static constexpr const char* kModGroupNames[kNumModGroups] = {
  "Filter & amp", "Oscillator & LFO", "Envelope", "Tape delay", "Digital delay", "Clouds", "Arp & limiter",
  "LFO 1", "LFO 2", "LFO 3", "Mod envelope", "Portamento"
};
static constexpr int kModGroupStart[kNumModGroups] = { 0, 6, 12, 16, 21, 24, 32, 37, 45, 53, 61, 68 };
static constexpr int kModGroupCount[kNumModGroups] = { 6, 6, 4, 5, 3, 8, 5, 8, 8, 8, 7, 1 };
static inline int modGroupOf(int t)
{ for (int g = 0; g < kNumModGroups; g++) if (t >= kModGroupStart[g] && t < kModGroupStart[g] + kModGroupCount[g]) return g; return 0; }
// The 37 original target values are retained exactly (t / 36). The 32 new
// destinations are encoded at unused midpoints so old sessions and host
// automation continue to select the same legacy targets after this expansion.
static inline float valueFromModTarget(int t)
{
  t = std::max(0, std::min(kNumModTargets - 1, t));
  if (t < 37) return static_cast<float>(t) / 36.f;
  return (static_cast<float>(t - 37) + 0.5f) / 36.f;
}
static inline int modTargetFromValue(float v)
{
  v = std::max(0.f, std::min(1.f, v));
  // Match stored choice anchors before nearest-neighbour decoding. A few new
  // targets live at midpoints between legacy values, so relying on floating-
  // point tie-breaking alone could reinterpret a saved route on some hosts.
  for (int t = 0; t < kNumModTargets; ++t)
    if (std::fabs(v - valueFromModTarget(t)) < 1.0e-4f) return t;

  int best = 0;
  float bestDist = std::fabs(v - valueFromModTarget(0));
  for (int t = 1; t < kNumModTargets; ++t)
  {
    float dist = std::fabs(v - valueFromModTarget(t));
    if (dist < bestDist) { best = t; bestDist = dist; }
  }
  return best;
}

} // namespace kr106
