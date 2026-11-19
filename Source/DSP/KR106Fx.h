#pragma once

// Extra effects of the FX 2 page: a clean stereo Chorus, a Phaser, a Spring Reverb and a Compressor.
// All take normalized 0..1 settings (the plugin parameters). Every effect fades in and out in 5 ms when it
// is switched, and passes the signal untouched (bit-exact) while it is off.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace kr106 {

struct FxFade
{
  float wet = 0.f, step = 0.01f;
  void Init(float sr) { step = 1.f / (0.005f * sr); wet = 0.f; }
  float Next(bool on)
  {
    const float goal = on ? 1.f : 0.f;
    if (wet < goal) wet = std::min(goal, wet + step); else if (wet > goal) wet = std::max(goal, wet - step);
    return wet;
  }
  bool Idle(bool on) const { return !on && wet <= 0.f; }
};

static inline float fxLerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float fxClamp01(float v) { return std::min(1.f, std::max(0.f, v)); }

// ---- Chorus: two modulated delays per side, opposite phases, a quarter turn between left and right -------
class FxChorus
{
public:
  void Init(float sr)
  {
    mSR = sr; mSize = static_cast<int>(0.06f * sr) + 8;
    mBuf[0].assign(static_cast<size_t>(mSize), 0.f); mBuf[1].assign(static_cast<size_t>(mSize), 0.f);
    mW = 0; mPh = 0.f; mFade.Init(sr);
  }
  void Set(bool on, float rate, float depth, float mix) { mOn = on; mRate = 0.1f * std::pow(50.f, fxClamp01(rate)); mDepth = 0.0003f + 0.0055f * fxClamp01(depth); mMix = fxClamp01(mix); }
  void Process(float& l, float& r)
  {
    const float wet = mFade.Next(mOn);
    mBuf[0][static_cast<size_t>(mW)] = l; mBuf[1][static_cast<size_t>(mW)] = r;
    mPh += mRate / mSR; if (mPh >= 1.f) mPh -= 1.f;
    if (wet > 0.f)
    {
      float out[2] = { l, r };
      for (int c = 0; c < 2; c++)
      {
        const float p = mPh + 0.25f * static_cast<float>(c);
        const float dA = (0.012f + mDepth * std::sin(6.2831853f * p)) * mSR;
        const float dB = (0.012f + mDepth * std::sin(6.2831853f * (p + 0.5f))) * mSR;
        const float w = 0.5f * (Read(c, dA) + Read(c, dB));
        const float m = mMix * wet;
        out[c] = (c == 0 ? l : r) * (1.f - 0.5f * m) + w * m * 0.9f;
      }
      l = out[0]; r = out[1];
    }
    mW = (mW + 1) % mSize;
  }
private:
  float Read(int c, float delaySamples) const
  {
    float pos = static_cast<float>(mW) - delaySamples;
    while (pos < 0.f) pos += static_cast<float>(mSize);
    const int i0 = static_cast<int>(pos), i1 = (i0 + 1) % mSize;
    const float fr = pos - static_cast<float>(i0);
    return fxLerp(mBuf[c][static_cast<size_t>(i0 % mSize)], mBuf[c][static_cast<size_t>(i1)], fr);
  }
  float mSR = 44100.f, mPh = 0.f, mRate = 0.5f, mDepth = 0.002f, mMix = 0.5f;
  int mSize = 2700, mW = 0;
  bool mOn = false;
  std::vector<float> mBuf[2];
  FxFade mFade;
};

// ---- Phaser: six all-pass stages swept by an LFO, feedback around them --------------------------------------
class FxPhaser
{
public:
  void Init(float sr)
  {
    mSR = sr; mFade.Init(sr); mPh = 0.f; mCount = 0;
    for (auto& ch : mZ) for (auto& z : ch) z = 0.f;
    mFb[0] = mFb[1] = 0.f; mA[0] = mA[1] = 0.f;
  }
  void Set(bool on, float rate, float depth, float fb, float mix) { mOn = on; mRate = 0.05f * std::pow(160.f, fxClamp01(rate)); mDepth = 0.15f + 0.85f * fxClamp01(depth); mFbAmt = 0.92f * fxClamp01(fb); mMix = fxClamp01(mix); }
  void Process(float& l, float& r)
  {
    const float wet = mFade.Next(mOn);
    mPh += mRate / mSR; if (mPh >= 1.f) mPh -= 1.f;
    if (mFade.Idle(mOn)) return;
    if ((mCount++ & 7) == 0)                                          // the all-pass frequency changes slowly: every 8 samples is plenty
      for (int c = 0; c < 2; c++)
      {
        const float lfo = std::sin(6.2831853f * (mPh + 0.25f * static_cast<float>(c)));
        const float fc = 220.f * std::pow(16.f, 0.5f + 0.5f * lfo * mDepth);
        const float t = std::tan(3.14159265f * std::min(fc, 0.45f * mSR) / mSR);
        mA[c] = (t - 1.f) / (t + 1.f);
      }
    float in[2] = { l, r }, out[2];
    for (int c = 0; c < 2; c++)
    {
      float x = in[c] + mFbAmt * mFb[c];
      for (int k = 0; k < kStages; k++)
      {
        const float y = mA[c] * x + mZ[c][k];
        mZ[c][k] = x - mA[c] * y;
        x = y;
      }
      mFb[c] = std::tanh(x);                                         // soft limit: the feedback can never run away
      const float m = mMix * wet;
      out[c] = in[c] * (1.f - 0.5f * m) + x * (0.5f * m);
    }
    l = out[0]; r = out[1];
  }
private:
  static constexpr int kStages = 6;
  float mSR = 44100.f, mPh = 0.f, mRate = 0.5f, mDepth = 0.6f, mFbAmt = 0.3f, mMix = 0.5f;
  float mZ[2][kStages] = {}, mFb[2] = {}, mA[2] = {};
  unsigned mCount = 0;
  bool mOn = false;
  FxFade mFade;
};

// ---- Spring Reverb: dispersive springs (a chain of all-passes inside a feedback delay) --------------------
class FxSpring
{
public:
  void Init(float sr)
  {
    mSR = sr; mFade.Init(sr); mPh = 0.f;
    static const float lens[2][2] = { { 0.0297f, 0.0413f }, { 0.0331f, 0.0459f } };        // seconds: left springs, right springs
    for (int c = 0; c < 2; c++)
      for (int s = 0; s < 2; s++)
      {
        Spring& sp = mSp[c][s];
        sp.size = static_cast<int>(0.07f * sr) + 8;
        sp.buf.assign(static_cast<size_t>(sp.size), 0.f);
        sp.len = lens[c][s] * sr; sp.w = 0; sp.lp = 0.f;
        for (auto& z : sp.z) z = 0.f;
      }
  }
  void Set(bool on, float decay, float tone, float drive, float mix)
  {
    mOn = on; mFbAmt = 0.3f + 0.64f * fxClamp01(decay);
    const float fc = 1500.f * std::pow(6.f, fxClamp01(tone));                                 // 1.5 .. 9 kHz
    mLpCoef = 1.f - std::exp(-6.2831853f * fc / mSR);
    mDrive = 1.f + 5.f * fxClamp01(drive); mMix = fxClamp01(mix);
  }
  void Process(float& l, float& r)
  {
    const float wet = mFade.Next(mOn);
    mPh += 0.9f / mSR; if (mPh >= 1.f) mPh -= 1.f;
    if (mFade.Idle(mOn)) return;
    const float norm = 1.f / std::tanh(mDrive);
    float in[2] = { l, r }, out[2];
    for (int c = 0; c < 2; c++)
    {
      const float x = std::tanh(mDrive * in[c]) * norm * 0.5f;                              // the drive saturates the spring input
      float sum = 0.f;
      for (int s = 0; s < 2; s++)
      {
        Spring& sp = mSp[c][s];
        const float wobble = 0.0003f * mSR * std::sin(6.2831853f * (mPh + 0.3f * static_cast<float>(s + 2 * c)));
        float pos = static_cast<float>(sp.w) - (sp.len + wobble);
        while (pos < 0.f) pos += static_cast<float>(sp.size);
        const int i0 = static_cast<int>(pos), i1 = (i0 + 1) % sp.size;
        const float y = fxLerp(sp.buf[static_cast<size_t>(i0 % sp.size)], sp.buf[static_cast<size_t>(i1)], pos - static_cast<float>(i0));
        sp.lp += mLpCoef * (y - sp.lp);
        float v = x + mFbAmt * sp.lp;
        for (int k = 0; k < kDisp; k++)                                                     // dispersion: high notes arrive before low ones
        {
          const float o = -kA * v + sp.z[k];
          sp.z[k] = v + kA * o;
          v = o;
        }
        sp.buf[static_cast<size_t>(sp.w)] = std::max(-8.f, std::min(8.f, v));
        sp.w = (sp.w + 1) % sp.size;
        sum += y;
      }
      const float m = mMix * wet;
      out[c] = in[c] * (1.f - 0.5f * m) + 0.9f * m * sum;
    }
    l = out[0]; r = out[1];
  }
private:
  static constexpr int kDisp = 24;
  static constexpr float kA = 0.6f;
  struct Spring { std::vector<float> buf; int size = 3000, w = 0; float len = 1300.f, lp = 0.f, z[kDisp] = {}; };
  Spring mSp[2][2];
  float mSR = 44100.f, mPh = 0.f, mFbAmt = 0.6f, mLpCoef = 0.3f, mDrive = 2.f, mMix = 0.3f;
  bool mOn = false;
  FxFade mFade;
};

// ---- Compressor: stereo-linked, soft knee, with make-up gain (sits before the limiter) ---------------------
class FxCompressor
{
public:
  void Init(float sr) { mSR = sr; mFade.Init(sr); mGrDb = 0.f; mGr.store(0.f); Update(); }
  void Set(bool on, float thr, float ratio, float attack, float release, float makeup)
  {
    mOn = on; mThr = -40.f * (1.f - fxClamp01(thr));                                   // -40 .. 0 dB
    mRatio = 1.5f * std::pow(13.3f, fxClamp01(ratio));                                // 1.5 .. 20
    mAtkMs = 0.5f * std::pow(160.f, fxClamp01(attack));                               // 0.5 .. 80 ms
    mRelMs = 20.f * std::pow(45.f, fxClamp01(release));                               // 20 .. 900 ms
    mMakeDb = 24.f * fxClamp01(makeup);
    Update();
  }
  float GetGainReductionDb() const { return mGr.load(std::memory_order_relaxed); }
  void Process(float& l, float& r)
  {
    const float wet = mFade.Next(mOn);
    if (mFade.Idle(mOn)) { mGr.store(0.f, std::memory_order_relaxed); return; }
    const float lvl = 20.f * std::log10(std::max(1e-6f, std::max(std::fabs(l), std::fabs(r))));
    const float over = lvl - mThr, knee = 6.f, slope = 1.f - 1.f / mRatio;
    float red = 0.f;                                                                   // wanted reduction in dB (positive)
    if (over > knee * 0.5f) red = over * slope;
    else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; red = x * x / (2.f * knee) * slope; }
    if (red > mGrDb) mGrDb += (red - mGrDb) * mAtk; else mGrDb += (red - mGrDb) * mRel;
    const float gain = std::pow(10.f, (mMakeDb - mGrDb) / 20.f);
    l = l * (1.f - wet) + l * gain * wet;
    r = r * (1.f - wet) + r * gain * wet;
    mGr.store(-mGrDb * wet, std::memory_order_relaxed);
  }
private:
  void Update()
  {
    mAtk = 1.f - std::exp(-1.f / (mAtkMs * 0.001f * mSR));
    mRel = 1.f - std::exp(-1.f / (mRelMs * 0.001f * mSR));
  }
  float mSR = 44100.f, mThr = -16.f, mRatio = 4.f, mAtkMs = 5.f, mRelMs = 150.f, mMakeDb = 0.f, mAtk = 0.01f, mRel = 0.001f, mGrDb = 0.f;
  bool mOn = false;
  FxFade mFade;
  std::atomic<float> mGr { 0.f };
};

} // namespace kr106
