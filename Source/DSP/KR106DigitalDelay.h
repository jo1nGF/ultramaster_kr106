#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

// KR-106 Digital Delay
//
// Clean stereo digital echo (the opposite character of the tape delay): exact
// timing, no modulation, no saturation. Placed after the tape delay.
//
//   in -> + -> delay line (Hermite) -> tap ---------------------------> wet
//         ^                              |
//         +-- feedback <- LP(tone) <- HP(low cut) <-+
//
// - Time 5 ms .. 1.5 s. Changing the time never glides in pitch and never
//   clicks: a second read head fades in (equal power, 25 ms) while the old one
//   fades out.
// - Feedback filters: "Tone" low-pass and "Low cut" high-pass sit in the loop
//   only, so the first repeat is clean and each following one gets darker /
//   thinner, like a real digital delay.
// - Modes: Stereo (L and R delay independently), Ping-pong (the repeats bounce
//   left <-> right) and Dotted (right channel at 3/4 of the left time).
// - A soft limiter in the loop stays out of the way (linear up to |x| = 2) but
//   keeps feedback near 1 from running away.
// - mix = 0 is a bit-exact bypass of the dry signal.
//
// Quality notes: read positions are doubles (sample-exact after hours of
// playing), all knobs are smoothed (no zipper noise).
// All parameters are normalized 0..1, as for the UI sliders.
namespace kr106 {

class DigitalDelay
{
public:
  static constexpr float kMinTimeMs = 5.f;
  static constexpr float kMaxTimeMs = 1500.f;
  static constexpr float kMaxFeedback = 0.97f;
  static constexpr int kNumModes = 3;     // Stereo, Ping-pong, Dotted

  static float TimeMs(float t)    { return kMinTimeMs * powf(kMaxTimeMs / kMinTimeMs, t); }
  static float ToneHz(float v)    { return 800.f * powf(25.f, v); }   // 0.8 k .. 20 kHz
  static float LowCutHz(float v)  { return 20.f * powf(25.f, v); }    // 20 .. 500 Hz
  static constexpr const char* kModeNames[kNumModes] = { "Stereo", "Ping-pong", "Dotted" };
  static int ModeFromValue(float v)
  { return std::max(0, std::min(kNumModes - 1, static_cast<int>(std::floor(v * (kNumModes - 1) + 0.5f)))); }
  static float ValueFromMode(int m) { return static_cast<float>(m) / (kNumModes - 1); }

  void Init(float sampleRate)
  {
    mSR = sampleRate;
    int need = static_cast<int>(sampleRate * kMaxTimeMs * 0.001f) + 16;
    int n = 1; while (n < need) n <<= 1;
    mMask = n - 1;
    for (int c = 0; c < 2; c++)
    {
      mBuf[c].assign(static_cast<size_t>(n), 0.f);
      mLP[c] = mHP[c] = 0.f;
      mFading[c] = false; mFadePos[c] = 0;
    }
    mW = 0;
    mFadeLen = std::max(16, static_cast<int>(0.025f * sampleRate));
    mParamCoeff = 1.f - expf(-1.f / (0.02f * sampleRate));
    Snap();
    for (int c = 0; c < 2; c++) mD0[c] = mD1[c] = Target(c);
  }

  void SetTime(float v)     { mTime = clamp01(v); }
  void SetFeedback(float v) { mFeedback = clamp01(v); }
  void SetMix(float v)      { mMix = clamp01(v); }
  void SetTone(float v)     { mTone = clamp01(v); }
  void SetLowCut(float v)   { mLowCut = clamp01(v); }
  void SetMode(float v)     { mMode = ModeFromValue(v); }

  // Jump smoothed values to their targets (after loading a state, on Init).
  void Snap()
  {
    mFbS = mFeedback * kMaxFeedback; mMixS = mMix;
    mLPS = LPCoeff(mTone); mHPS = HPCoeff(mLowCut);
    for (int c = 0; c < 2; c++) { mD0[c] = Target(c); mFading[c] = false; }
  }

  void Process(float inL, float inR, float& outL, float& outR)
  {
    mFbS  += (mFeedback * kMaxFeedback - mFbS) * mParamCoeff;
    mMixS += (mMix - mMixS) * mParamCoeff;
    mLPS  += (LPCoeff(mTone) - mLPS) * mParamCoeff;
    mHPS  += (HPCoeff(mLowCut) - mHPS) * mParamCoeff;

    const float tap[2] = { ReadLine(0), ReadLine(1) };

    // feedback path: low cut (HP) then tone (LP), per channel
    float fb[2];
    for (int c = 0; c < 2; c++)
    {
      mHP[c] += mHPS * (tap[c] - mHP[c]);
      float hp = tap[c] - mHP[c];
      mLP[c] += mLPS * (hp - mLP[c]);
      fb[c] = SoftLimit(mLP[c] * mFbS);
    }

    float w[2];
    if (mMode == 1)    // ping-pong: input into L, then L -> R -> L ...
    {
      const float mono = 0.5f * (inL + inR);
      w[0] = mono + fb[1];
      w[1] = fb[0];
    }
    else               // stereo / dotted: independent channels
    {
      w[0] = inL + fb[0];
      w[1] = inR + fb[1];
    }
    for (int c = 0; c < 2; c++)
    {
      if (std::fabs(w[c]) < 1e-18f) w[c] = 0.f;
      mBuf[c][static_cast<size_t>(mW & mMask)] = w[c];
    }
    mW++;

    outL = inL + tap[0] * mMixS;
    outR = inR + tap[1] * mMixS;
  }

private:
  static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
  float LPCoeff(float tone) const
  { return 1.f - expf(-2.f * 3.14159265f * std::min(ToneHz(tone), 0.45f * mSR) / mSR); }
  float HPCoeff(float low) const
  { return 1.f - expf(-2.f * 3.14159265f * LowCutHz(low) / mSR); }

  // Linear up to |x| = 2, then a smooth knee towards +-4.
  static float SoftLimit(float x)
  {
    float a = std::fabs(x);
    if (a <= 2.f) return x;
    float y = 2.f + 2.f * std::tanh((a - 2.f) * 0.5f);
    return x < 0.f ? -y : y;
  }

  // Wanted delay (samples) of line c
  double Target(int c) const
  {
    double t = static_cast<double>(TimeMs(mTime)) * 0.001 * mSR;
    if (mMode == 2 && c == 1) t *= 0.75;           // dotted: right = 3/4 of left
    return std::max(4.0, t);
  }

  // Read one line; starts a cross-fade when the wanted time has moved.
  float ReadLine(int c)
  {
    const auto& b = mBuf[c];
    const double target = Target(c);
    if (!mFading[c] && std::fabs(target - mD0[c]) > 0.5)
    {
      mD1[c] = target; mFading[c] = true; mFadePos[c] = 0;
    }
    if (!mFading[c]) return Hermite(b, mD0[c]);

    const float f = static_cast<float>(mFadePos[c]) / static_cast<float>(mFadeLen);
    const float gA = std::cos(1.5707963f * f), gB = std::sin(1.5707963f * f);
    const float y = gA * Hermite(b, mD0[c]) + gB * Hermite(b, mD1[c]);
    if (++mFadePos[c] >= mFadeLen) { mD0[c] = mD1[c]; mFading[c] = false; }
    return y;
  }

  float Hermite(const std::vector<float>& b, double d) const
  {
    double rp = static_cast<double>(mW) - d;
    long long ip = static_cast<long long>(std::floor(rp));
    float f = static_cast<float>(rp - static_cast<double>(ip));
    float y0 = b[static_cast<size_t>((ip - 1) & mMask)], y1 = b[static_cast<size_t>(ip & mMask)];
    float y2 = b[static_cast<size_t>((ip + 1) & mMask)], y3 = b[static_cast<size_t>((ip + 2) & mMask)];
    float c0 = y1, c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
  }

  float mSR = 44100.f;
  std::vector<float> mBuf[2];
  long long mW = 0, mMask = 0;
  float mTime = 0.75f, mFeedback = 0.35f, mMix = 0.f, mTone = 0.8f, mLowCut = 0.2f;
  int mMode = 0;
  float mFbS = 0.f, mMixS = 0.f, mLPS = 0.5f, mHPS = 0.f, mParamCoeff = 0.f;
  float mLP[2] = {}, mHP[2] = {};
  double mD0[2] = { 1000., 1000. }, mD1[2] = { 1000., 1000. };
  bool mFading[2] = { false, false };
  int mFadePos[2] = { 0, 0 }, mFadeLen = 1200;
};

} // namespace kr106
