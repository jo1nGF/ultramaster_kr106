#pragma once

#include <cmath>
#include <algorithm>
#include <atomic>
#include <vector>

// KR-106 master limiter (last stage of the chain).
//
// Look-ahead peak limiter: the audio is delayed by ~1.5 ms while a gain is
// computed from the loudest sample in that window, so the gain is already down
// when a peak arrives (no overshoot, no pumping on slow material). Attack is a
// fast smoothing that finishes well inside the look-ahead, release is
// exponential (20 .. 800 ms). A final clamp to the ceiling is the safety net.
//
//  - Off: bit-exact bypass. Switching on/off crossfades (5 ms) so it never clicks.
//  - Ceiling: -12 .. 0 dBFS.
//  - GetGainReductionDb(): for the UI meter.
namespace kr106 {

class Limiter
{
public:
  static float CeilingDb(float v) { return -12.f + 12.f * v; }              // -12 .. 0 dB
  static float ReleaseMs(float v) { return 20.f * powf(40.f, v); }          // 20 .. 800 ms

  void Init(float sampleRate)
  {
    mSR = sampleRate;
    mLook = std::max(8, static_cast<int>(0.0015f * sampleRate));
    const size_t n = static_cast<size_t>(mLook + 1);
    mBuf[0].assign(n, 0.f); mBuf[1].assign(n, 0.f); mPk.assign(n, 0.f);
    mW = 0; mGain = 1.f; mWet = mOn ? 1.f : 0.f;
    mAttack = 1.f - expf(-1.f / (0.0005f * sampleRate));   // ~0.5 ms
    mXfade = 1.f / (0.005f * sampleRate);                  // 5 ms crossfade on/off
    mGrDb.store(0.f);
    Update();
  }
  void SetOn(bool on)       { mOn = on; }
  void SetCeiling(float v)  { mCeilV = std::min(1.f, std::max(0.f, v)); Update(); }
  void SetRelease(float v)  { mRelV = std::min(1.f, std::max(0.f, v)); Update(); }
  float GetGainReductionDb() const { return mGrDb.load(std::memory_order_relaxed); }
  int   GetLatencySamples() const  { return mOn ? mLook : 0; }

  void Process(float& l, float& r)
  {
    const size_t N = static_cast<size_t>(mLook + 1);
    // 1. write the new sample; the slot after it holds the one written mLook samples ago
    mBuf[0][mW] = l; mBuf[1][mW] = r;
    mPk[mW] = std::max(std::fabs(l), std::fabs(r));
    const size_t rd = (mW + 1) % N;
    const float dl = mBuf[0][rd], dr = mBuf[1][rd];
    mW = rd;

    // 2. gain from the loudest sample in the window (the delayed one + everything after it)
    float pk = 0.f;
    for (float v : mPk) pk = std::max(pk, v);
    const float target = pk > mCeil ? mCeil / pk : 1.f;
    if (target < mGain) mGain += (target - mGain) * mAttack;       // fast: anticipates the peak
    else                mGain += (target - mGain) * mRelCoef;      // slow release
    if (mGain > target) mGain = target;                            // never above what the loudest pending sample needs

    float ol = std::max(-mCeil, std::min(mCeil, dl * mGain));      // safety net
    float orr = std::max(-mCeil, std::min(mCeil, dr * mGain));

    // 3. crossfade between the untouched input (off) and the limited, delayed signal (on)
    const float goal = mOn ? 1.f : 0.f;
    if (mWet < goal) mWet = std::min(goal, mWet + mXfade); else if (mWet > goal) mWet = std::max(goal, mWet - mXfade);
    if (mWet > 0.f)
    {
      l = l * (1.f - mWet) + ol * mWet;
      r = r * (1.f - mWet) + orr * mWet;
    }
    mGrDb.store((mOn && mGain < 1.f) ? 20.f * log10f(std::max(mGain, 1e-4f)) : 0.f, std::memory_order_relaxed);
  }

private:
  void Update()
  {
    mCeil = powf(10.f, CeilingDb(mCeilV) / 20.f);
    mRelCoef = 1.f - expf(-1.f / (ReleaseMs(mRelV) * 0.001f * mSR));
  }

  float mSR = 44100.f;
  std::vector<float> mBuf[2], mPk;
  int mLook = 72;
  size_t mW = 0;
  float mGain = 1.f, mCeil = 0.97f, mAttack = 0.1f, mRelCoef = 0.001f, mWet = 0.f, mXfade = 0.01f;
  float mCeilV = 0.97f, mRelV = 0.3f;
  bool mOn = false;
  std::atomic<float> mGrDb{0.f};
};

} // namespace kr106
