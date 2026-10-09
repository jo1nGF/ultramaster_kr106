#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

// KR-106 Tape Delay
//
// Stereo tape-style echo placed after the chorus, before master volume.
//
//   in -> [dry] ------------------------------------> out
//   in -> + -> delay line (Hermite) -> tap -> HPF -> LPF(tone) -> sat -+
//         ^                                                            |
//         +------------------- feedback <------------------------------+
//   out = dry + mix * tap_after_filters
//
// - Time is smoothed with a slow one-pole (tape-speed inertia), so moving
//   the knob bends the pitch of the repeats like a real tape machine.
// - Wow (~0.6 Hz) and flutter (~6.5 Hz) plus a little filtered noise modulate
//   the read position; L and R use different phases for stereo width.
// - Feedback path: 1-pole HPF (~110 Hz) stops LF build-up, tone LPF darkens
//   each repeat, and a soft saturator keeps high feedback bounded.
// - mix = 0 is a bit-exact bypass of the dry signal (state keeps running so
//   engaging the effect is click-free).
//
// All parameters are normalized 0..1 (same convention as the UI sliders).
namespace kr106 {

class TapeDelay
{
public:
  static constexpr float kMinTimeMs = 40.f;
  static constexpr float kMaxTimeMs = 800.f;
  static constexpr float kMaxFeedback = 0.93f;

  static float TimeMs(float t) { return kMinTimeMs * powf(kMaxTimeMs / kMinTimeMs, t); }

  void Init(float sampleRate)
  {
    mSR = sampleRate;
    int need = static_cast<int>(sampleRate * (kMaxTimeMs + 12.f) * 0.001f) + 8;
    int n = 1; while (n < need) n <<= 1;
    mMask = n - 1;
    for (int c = 0; c < 2; c++) { mBuf[c].assign(n, 0.f); mHP[c] = mLP[c] = 0.f; }
    mW = 0;
    mTimeSmooth = TimeMs(mTime);
    mTimeCoeff = 1.f - expf(-1.f / (0.25f * sampleRate));   // ~250 ms tau
    mParamCoeff = 1.f - expf(-1.f / (0.01f * sampleRate));  // 10 ms tau
    mHPCoeff = 1.f - expf(-2.f * 3.14159265f * 110.f / sampleRate);
    mFbS = mFeedback * kMaxFeedback; mMixS = mMix; mLPCoeffS = LPCoeff(mTone);
    mPhaseWow = 0.f; mPhaseFlut = 0.25f; mNoiseState = 0.f; mRng = 22222u;
  }

  void SetTime(float v)     { mTime = clamp01(v); }
  void SetFeedback(float v) { mFeedback = clamp01(v); }
  void SetMix(float v)      { mMix = clamp01(v); }
  void SetTone(float v)     { mTone = clamp01(v); }
  void SetWow(float v)      { mWow = clamp01(v); }
  // Jump smoothed values to their targets (call after loading a preset /
  // on Init so the first repeats are not bent by the tape-speed glide).
  void Snap() { mTimeSmooth = TimeMs(mTime); mFbS = mFeedback * kMaxFeedback; mMixS = mMix; mLPCoeffS = LPCoeff(mTone); }

  void Process(float inL, float inR, float& outL, float& outR)
  {
    // parameter smoothing
    mFbS += (mFeedback * kMaxFeedback - mFbS) * mParamCoeff;
    mMixS += (mMix - mMixS) * mParamCoeff;
    mLPCoeffS += (LPCoeff(mTone) - mLPCoeffS) * mParamCoeff;
    mTimeSmooth += (TimeMs(mTime) - mTimeSmooth) * mTimeCoeff;

    // tape transport modulation (ms)
    mPhaseWow += 0.6f / mSR;   if (mPhaseWow >= 1.f) mPhaseWow -= 1.f;
    mPhaseFlut += 6.5f / mSR;  if (mPhaseFlut >= 1.f) mPhaseFlut -= 1.f;
    mRng = mRng * 1664525u + 1013904223u;
    float white = (static_cast<float>(mRng >> 8) / 8388608.f) - 1.f;
    mNoiseState += 0.0008f * (white - mNoiseState);
    const float wowDepth = 2.5f * mWow, flutDepth = 0.15f * mWow, nzDepth = 6.f * mWow;
    const float twoPi = 6.2831853f;

    float in[2] = { inL, inR }, out[2];
    for (int c = 0; c < 2; c++)
    {
      float ph = c ? 0.37f : 0.f;
      float modMs = wowDepth * sinf(twoPi * (mPhaseWow + ph))
                  + flutDepth * sinf(twoPi * (mPhaseFlut + ph * 1.7f))
                  + nzDepth * mNoiseState * (c ? -1.f : 1.f);
      float dSamp = std::max(2.f, (mTimeSmooth + modMs) * 0.001f * mSR);
      float tap = Read(mBuf[c], dSamp);

      // feedback path filters
      mHP[c] += mHPCoeff * (tap - mHP[c]);
      float hp = tap - mHP[c];
      mLP[c] += mLPCoeffS * (hp - mLP[c]);
      float wet = mLP[c];

      float fb = Sat(wet * mFbS);
      float w = in[c] + fb;
      if (std::fabs(w) < 1e-18f) w = 0.f;
      mBuf[c][mW & mMask] = w;

      out[c] = in[c] + wet * mMixS;
    }
    mW++;
    outL = out[0]; outR = out[1];
  }

private:
  static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
  float LPCoeff(float tone) const
  {
    float fc = 1200.f * powf(10.f, tone);  // 1.2 kHz .. 12 kHz
    fc = std::min(fc, 0.45f * mSR);
    return 1.f - expf(-2.f * 3.14159265f * fc / mSR);
  }
  // rational soft clipper, ~linear below 0.5, bounded at +-1
  static float Sat(float x)
  {
    x = std::min(2.5f, std::max(-2.5f, x));
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
  }
  float Read(const std::vector<float>& b, float d) const
  {
    float rp = static_cast<float>(mW) - d;
    long long ip = static_cast<long long>(std::floor(rp));
    float f = rp - static_cast<float>(ip);
    float y0 = b[(ip - 1) & mMask], y1 = b[ip & mMask], y2 = b[(ip + 1) & mMask], y3 = b[(ip + 2) & mMask];
    float c0 = y1, c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
  }

  float mSR = 44100.f;
  std::vector<float> mBuf[2];
  long long mW = 0; int mMask = 0;
  float mTime = 0.45f, mFeedback = 0.35f, mMix = 0.f, mTone = 0.6f, mWow = 0.2f;
  float mTimeSmooth = 200.f, mTimeCoeff = 0.f, mParamCoeff = 0.f, mHPCoeff = 0.f;
  float mFbS = 0.f, mMixS = 0.f, mLPCoeffS = 0.5f;
  float mHP[2] = {}, mLP[2] = {};
  float mPhaseWow = 0.f, mPhaseFlut = 0.f, mNoiseState = 0.f; unsigned mRng = 1;
};

} // namespace kr106
