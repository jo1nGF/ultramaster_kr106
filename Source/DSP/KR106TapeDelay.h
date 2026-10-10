#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

// KR-106 Tape Delay
//
// Stereo tape-style echo placed after the chorus, before master volume.
//
//   in -> [dry] ------------------------------------------> out
//   in -> + -> delay line (Hermite) -> tap -> HPF -> LPF(tone) -> soft sat -+
//         ^                                                                 |
//         +----------------------- feedback <-------------------------------+
//   out = dry + mix * tap_after_filters
//
// - Time is smoothed with a slow one-pole (tape-speed inertia), so moving the
//   knob bends the pitch of the repeats like a real tape machine.
// - WOW is a real delay-time LFO: a sine (plus a slightly detuned second one
//   and a slow random drift) modulates the read position, which is heard as
//   pitch vibrato of the repeats. Depth (knob "Wow") goes from subtle to a
//   strong warble; "Rate" sets the LFO speed. L and R run 90 degrees apart
//   for a rotating stereo wobble. A little 7 Hz flutter rides on top.
// - Feedback path: 1-pole HPF (~110 Hz) stops LF build-up, tone LPF darkens
//   each repeat, and a gentle tanh-like saturator bounds high feedback.
// - mix = 0 is a bit-exact bypass of the dry signal (state keeps running so
//   engaging the effect is click-free).
//
// Quality notes: the read position is computed in double precision (a float
// loses sample accuracy after ~6 minutes of playback and sounds noisy), and
// every parameter that can be moved while playing is smoothed (no zipper noise).
//
// All parameters are normalized 0..1 (same convention as the UI sliders).
namespace kr106 {

class TapeDelay
{
public:
  static constexpr float kMinTimeMs = 40.f;
  static constexpr float kMaxTimeMs = 800.f;
  static constexpr float kMaxFeedback = 0.93f;
  static constexpr float kMaxWowMs = 10.f;   // peak delay-time excursion at Wow = 1

  static float TimeMs(float t)    { return kMinTimeMs * powf(kMaxTimeMs / kMinTimeMs, t); }
  static float WowRateHz(float r) { return 0.2f * powf(30.f, r); }          // 0.2 .. 6 Hz
  static float WowDepthMs(float w){ return kMaxWowMs * w * sqrtf(w); }      // w^1.5

  void Init(float sampleRate)
  {
    mSR = sampleRate;
    int need = static_cast<int>(sampleRate * (kMaxTimeMs + kMaxWowMs + 8.f) * 0.001f) + 8;
    int n = 1; while (n < need) n <<= 1;
    mMask = n - 1;
    for (int c = 0; c < 2; c++) { mBuf[c].assign(n, 0.f); mHP[c] = mLP[c] = 0.f; }
    mW = 0;
    mTimeCoeff  = 1.f - expf(-1.f / (0.25f * sampleRate));   // ~250 ms: tape inertia
    mParamCoeff = 1.f - expf(-1.f / (0.02f * sampleRate));   // 20 ms
    mHPCoeff = 1.f - expf(-2.f * 3.14159265f * 110.f / sampleRate);
    mDriftCoeff = 1.f - expf(-2.f * 3.14159265f * 0.35f / sampleRate);
    Snap();
    mPhaseWow = 0.f; mPhaseWow2 = 0.31f; mPhaseFlut = 0.25f; mDrift = 0.f; mRng = 22222u;
  }

  void SetTime(float v)     { mTime = clamp01(v); }
  void SetFeedback(float v) { mFeedback = clamp01(v); }
  void SetMix(float v)      { mMix = clamp01(v); }
  void SetTone(float v)     { mTone = clamp01(v); }
  void SetWow(float v)      { mWow = clamp01(v); }
  void SetWowRate(float v)  { mWowRate = clamp01(v); }

  // Jump smoothed values to their targets (call after loading a preset /
  // on Init so the first repeats are not bent by the tape-speed glide).
  void Snap()
  {
    mTimeSmooth = TimeMs(mTime); mFbS = mFeedback * kMaxFeedback; mMixS = mMix;
    mLPCoeffS = LPCoeff(mTone); mWowDepthS = WowDepthMs(mWow); mWowRateS = WowRateHz(mWowRate);
  }

  void Process(float inL, float inR, float& outL, float& outR)
  {
    // --- parameter smoothing (all of them) ---
    mFbS       += (mFeedback * kMaxFeedback - mFbS) * mParamCoeff;
    mMixS      += (mMix - mMixS) * mParamCoeff;
    mLPCoeffS  += (LPCoeff(mTone) - mLPCoeffS) * mParamCoeff;
    mWowDepthS += (WowDepthMs(mWow) - mWowDepthS) * mParamCoeff;
    mWowRateS  += (WowRateHz(mWowRate) - mWowRateS) * mParamCoeff;
    mTimeSmooth += (TimeMs(mTime) - mTimeSmooth) * mTimeCoeff;

    // --- tape transport modulation ---
    mPhaseWow  += mWowRateS / mSR;          if (mPhaseWow  >= 1.f) mPhaseWow  -= 1.f;
    mPhaseWow2 += mWowRateS * 1.37f / mSR;  if (mPhaseWow2 >= 1.f) mPhaseWow2 -= 1.f;
    mPhaseFlut += 7.1f / mSR;               if (mPhaseFlut >= 1.f) mPhaseFlut -= 1.f;
    mRng = mRng * 1664525u + 1013904223u;
    float white = (static_cast<float>(mRng >> 8) / 8388608.f) - 1.f;
    mDrift += mDriftCoeff * (white * 3.f - mDrift);      // slow random drift (~0.35 Hz)
    const float wowMs   = mWowDepthS;
    const float flutMs  = 0.12f * (wowMs / kMaxWowMs) * 4.f;   // grows with Wow, tiny
    const float driftMs = 0.12f * wowMs;
    const float twoPi = 6.2831853f;

    float in[2] = { inL, inR }, out[2];
    for (int c = 0; c < 2; c++)
    {
      const float ph = c ? 0.25f : 0.f;      // R is 90 degrees behind L
      float modMs = wowMs * (0.8f * sinf(twoPi * (mPhaseWow + ph))
                           + 0.2f * sinf(twoPi * (mPhaseWow2 + ph * 1.3f)))
                  + flutMs * sinf(twoPi * (mPhaseFlut + ph * 1.7f))
                  + driftMs * mDrift * (c ? -1.f : 1.f);
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
      mBuf[c][static_cast<size_t>(mW & mMask)] = w;

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
  // tanh-like soft clipper with a wide linear region (<5% error up to |x|=1),
  // bounded at +-3. Rational approximation of 3*tanh(x/3).
  static float Sat(float x)
  {
    float z = std::min(3.f, std::max(-3.f, x * (1.f / 3.f)));
    float z2 = z * z;
    return 3.f * z * (27.f + z2) / (27.f + 9.f * z2);
  }
  // 4-point Hermite. The read position is computed in double so it stays
  // sample-accurate no matter how long the synth has been running.
  float Read(const std::vector<float>& b, float d) const
  {
    double rp = static_cast<double>(mW) - static_cast<double>(d);
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
  long long mW = 0; long long mMask = 0;
  float mTime = 0.45f, mFeedback = 0.35f, mMix = 0.f, mTone = 0.6f, mWow = 0.2f, mWowRate = 0.35f;
  float mTimeSmooth = 200.f, mTimeCoeff = 0.f, mParamCoeff = 0.f, mHPCoeff = 0.f, mDriftCoeff = 0.f;
  float mFbS = 0.f, mMixS = 0.f, mLPCoeffS = 0.5f, mWowDepthS = 0.f, mWowRateS = 1.f;
  float mHP[2] = {}, mLP[2] = {};
  float mPhaseWow = 0.f, mPhaseWow2 = 0.f, mPhaseFlut = 0.f, mDrift = 0.f; unsigned mRng = 1;
};

} // namespace kr106
