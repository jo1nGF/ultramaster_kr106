#pragma once

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

// KR-106 "Clouds": a granular texture processor (in the spirit of Mutable
// Instruments Clouds), placed near the end of the effect chain.
//
// The input is recorded into a 2-second stereo circular buffer. Up to 32
// overlapping grains replay small windowed slices of it:
//   Position  where in the buffer the grains start (0 = just now ... 1 = 2 s ago)
//   Size      grain length, 20 ms .. 1.2 s
//   Pitch     grain playback rate, -24 .. +24 semitones (centre = unchanged)
//   Density   grains per second, ~1 .. 80
//   Texture   window shape + randomness (position / pitch jitter, spawn timing)
//   Spread    random stereo placement of each grain
//   Feedback  grain output back into the buffer
//   Reverb    amount of a stereo reverb on the grain cloud
//   Mix       dry / wet (0 = bit-exact bypass)
//   Freeze    stop recording: the buffer content is looped by the grains
//
// All parameters are normalized 0..1. Reads use linear interpolation; read
// positions are doubles (sample-exact however long the effect has been running).
#ifndef KR106_CLOUDS_NORM_EXP
#define KR106_CLOUDS_NORM_EXP 0.5f   // grain-count normalisation exponent (0.5 = incoherent, 1 = coherent)
#endif

namespace kr106 {

// Small Freeverb-style stereo reverb for the cloud
class CloudReverb
{
public:
  void Init(float sr)
  {
    static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int apT[4]   = { 556, 441, 341, 225 };
    const float k = sr / 44100.f;
    for (int c = 0; c < 2; c++)
    {
      for (int i = 0; i < 8; i++) { mComb[c][i].assign(static_cast<size_t>(combT[i] * k) + (c ? 23 : 0) + 1, 0.f); mCombPos[c][i] = 0; mCombLP[c][i] = 0.f; }
      for (int i = 0; i < 4; i++) { mAp[c][i].assign(static_cast<size_t>(apT[i] * k) + (c ? 23 : 0) + 1, 0.f); mApPos[c][i] = 0; }
    }
  }
  void Process(float in, float& outL, float& outR)
  {
    const float x = in * 0.015f;
    float o[2];
    for (int c = 0; c < 2; c++)
    {
      float sum = 0.f;
      for (int i = 0; i < 8; i++)
      {
        auto& b = mComb[c][i]; size_t& p = mCombPos[c][i];
        float y = b[p];
        mCombLP[c][i] = y * (1.f - kDamp) + mCombLP[c][i] * kDamp;
        b[p] = x + mCombLP[c][i] * kRoom;
        if (++p >= b.size()) p = 0;
        sum += y;
      }
      for (int i = 0; i < 4; i++)
      {
        auto& b = mAp[c][i]; size_t& p = mApPos[c][i];
        float bo = b[p];
        b[p] = sum + bo * 0.5f;
        sum = bo - sum;
        if (++p >= b.size()) p = 0;
      }
      o[c] = sum * 3.f;
    }
    outL = o[0]; outR = o[1];
  }
private:
  static constexpr float kRoom = 0.86f, kDamp = 0.3f;
  std::vector<float> mComb[2][8], mAp[2][4];
  size_t mCombPos[2][8] = {}, mApPos[2][4] = {};
  float mCombLP[2][8] = {};
};

class Clouds
{
public:
  static constexpr int kMaxGrains = 32;
  static constexpr float kBufSeconds = 2.0f;

  void Init(float sampleRate)
  {
    mSR = sampleRate;
    int need = static_cast<int>(sampleRate * kBufSeconds) + 64;
    int n = 1; while (n < need) n <<= 1;
    mMask = static_cast<long long>(n - 1);
    mBufLen = static_cast<double>(n);
    mUsable = mBufLen - 64.0;
    for (int c = 0; c < 2; c++) mBuf[c].assign(static_cast<size_t>(n), 0.f);
    mHead = 0;
    for (auto& g : mGrains) g.active = false;
    mReverb.Init(sampleRate);
    mCoeff = 1.f - expf(-1.f / (0.02f * sampleRate));
    mSpawnAcc = 0.f; mRng = 0x2F6E2B1u; mActSmooth = 1.f;
    BuildWindow();
    Snap();
  }

  void SetPosition(float v) { mP[0] = clamp01(v); }
  void SetSize(float v)     { mP[1] = clamp01(v); }
  void SetPitch(float v)    { mP[2] = clamp01(v); }
  void SetDensity(float v)  { mP[3] = clamp01(v); }
  void SetTexture(float v)  { mP[4] = clamp01(v); }
  void SetSpread(float v)   { mP[5] = clamp01(v); }
  void SetFeedback(float v) { mP[6] = clamp01(v); }
  void SetReverb(float v)   { mP[7] = clamp01(v); }
  void SetMix(float v)      { mP[8] = clamp01(v); }
  void SetFreeze(bool f)    { mFreeze = f; }
  bool IsFrozen() const     { return mFreeze; }
  void Snap() { for (int i = 0; i < 9; i++) mS[i] = mP[i]; }

  // ---- display data for the UI (updated every 256 samples) ----
  struct Viz { float pos[kMaxGrains]; float amp[kMaxGrains]; int count; bool frozen; };
  void GetViz(Viz& out) const
  {
    out.count = mVizCount.load(std::memory_order_relaxed);
    for (int i = 0; i < kMaxGrains; i++)
    {
      out.pos[i] = mVizPos[i].load(std::memory_order_relaxed);
      out.amp[i] = mVizAmp[i].load(std::memory_order_relaxed);
    }
    out.frozen = mFreeze;
  }

  void Process(float inL, float inR, float& outL, float& outR)
  {
    for (int i = 0; i < 9; i++) mS[i] += (mP[i] - mS[i]) * mCoeff;
    const float sizeP = mS[1], pitchP = mS[2], densP = mS[3], texP = mS[4];

    // ---- spawn new grains ----
    const float rateHz = 1.f + 79.f * densP * densP;
    mSpawnAcc += rateHz / mSR;
    if (mSpawnAcc >= 1.f)
    {
      mSpawnAcc -= 1.f + texP * 0.6f * (Rand01() - 0.5f);      // jittered timing with texture
      if (mSpawnAcc < 0.f) mSpawnAcc = 0.f;
      Spawn();
    }

    // ---- sum the grains ----
    float gl = 0.f, gr = 0.f; int active = 0;
    const double head = static_cast<double>(mHead);
    for (int gi = 0; gi < kMaxGrains; gi++)
    {
      Grain& g = mGrains[gi];
      if (!g.active) continue;
      const double delay = head - g.readIdx;
      if (g.age >= g.size || delay < 1.0 || delay > mUsable) { g.active = false; continue; }
      const float ph = g.age / g.size;
      const float w = WindowAt(ph);
      const long long i0 = static_cast<long long>(std::floor(g.readIdx));
      const float f = static_cast<float>(g.readIdx - static_cast<double>(i0));
      const size_t a = static_cast<size_t>(i0 & mMask), b = static_cast<size_t>((i0 + 1) & mMask);
      const float sl = mBuf[0][a] + (mBuf[0][b] - mBuf[0][a]) * f;
      const float sr = mBuf[1][a] + (mBuf[1][b] - mBuf[1][a]) * f;
      gl += sl * w * g.gl; gr += sr * w * g.gr;
      g.readIdx += g.rate; g.age += 1.f;
      active++;
    }
    mActSmooth += (static_cast<float>(std::max(1, active)) - mActSmooth) * 0.0008f;
    // Overlapping grains add up between sqrt(N) (noise) and N (the same tone); N^-0.5 times a
    // +3.5 dB make-up keeps most settings within a few dB of the input level.
    const float norm = 1.f / std::pow(std::max(1.f, mActSmooth), KR106_CLOUDS_NORM_EXP);
    gl *= norm * 1.5f; gr *= norm * 1.5f;

    // ---- feedback into the buffer, then record ----
    if (!mFreeze)
    {
      const float fbAmt = mS[6] * 0.9f;
      const float fl = SoftClip(gl * fbAmt), fr = SoftClip(gr * fbAmt);
      const size_t p = static_cast<size_t>(mHead & mMask);
      float wl = inL + fl, wr = inR + fr;
      if (std::fabs(wl) < 1e-18f) wl = 0.f;
      if (std::fabs(wr) < 1e-18f) wr = 0.f;
      mBuf[0][p] = wl; mBuf[1][p] = wr;
      mHead++;
    }

    // ---- reverb on the cloud ----
    float rl, rr;
    mReverb.Process(0.5f * (gl + gr), rl, rr);
    const float rev = mS[7];
    const float wl2 = WetLimit(gl * (1.f - 0.4f * rev) + rl * rev);
    const float wr2 = WetLimit(gr * (1.f - 0.4f * rev) + rr * rev);

    // ---- mix (0 = untouched input) ----
    const float mix = mS[8];
    outL = inL * (1.f - mix) + wl2 * mix;
    outR = inR * (1.f - mix) + wr2 * mix;

    if ((++mVizTick & 255) == 0) UpdateViz();
  }

private:
  struct Grain
  {
    bool active = false;
    double readIdx = 0.0;
    double rate = 1.0;
    float age = 0.f, size = 1.f, gl = 1.f, gr = 1.f;
  };

  static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
  static float SoftClip(float x) { return x / (1.f + std::fabs(x)); }
  // Keeps a runaway cloud (feedback at max) bounded: linear to 0.9, then a smooth knee up to 1.5
  static float WetLimit(float x)
  {
    const float a = std::fabs(x);
    if (a <= 0.9f) return x;
    const float y = 0.9f + 0.6f * std::tanh((a - 0.9f) / 0.6f);
    return x < 0.f ? -y : y;
  }
  float Rand01() { mRng = mRng * 1664525u + 1013904223u; return static_cast<float>(mRng >> 8) * (1.f / 16777216.f); }

  void BuildWindow()
  {
    for (int i = 0; i < kWin; i++)
      mWin[i] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(kWin - 1));
  }
  // Hann window, narrowed (more "plucked") as Texture rises
  float WindowAt(float ph) const
  {
    float x = ph * static_cast<float>(kWin - 1);
    int i = std::min(kWin - 2, static_cast<int>(x));
    float f = x - static_cast<float>(i);
    float w = mWin[i] + (mWin[i + 1] - mWin[i]) * f;
    const float t = mS[4];
    return t < 0.01f ? w : std::pow(w, 1.f + 1.5f * t);
  }

  void Spawn()
  {
    int slot = -1;
    for (int i = 0; i < kMaxGrains; i++) if (!mGrains[i].active) { slot = i; break; }
    if (slot < 0) return;                                          // all busy: skip this one
    Grain& g = mGrains[slot];
    const float tex = mS[4];

    // pitch: -24 .. +24 semitones around the centre, plus a little random detune with texture
    float semis = (mS[2] - 0.5f) * 48.f;
    semis += (Rand01() - 0.5f) * tex * 0.8f;
    const double rate = std::pow(2.0, semis / 12.0);

    // size (samples), limited so the grain always fits inside the buffer
    double size = (0.02 + 1.2 * mS[1] * mS[1]) * mSR;
    size = std::min(size, (mUsable - 8.0) / (std::max(1.0, rate) + 1.0));
    size = std::max(32.0, size);

    // start delay (how far back from the write head the grain begins)
    const double drift = std::fabs(rate - 1.0) * size;
    double dLo, dHi;
    if (mFreeze) { dLo = 2.0 + rate * size; dHi = mUsable; }
    else         { dLo = 2.0 + std::max(0.0, rate - 1.0) * size; dHi = mUsable - std::max(0.0, 1.0 - rate) * size; }
    (void)drift;
    if (dHi < dLo) dHi = dLo;
    float pos = mS[0] + (Rand01() - 0.5f) * tex * 0.4f;
    pos = clamp01(pos);
    const double d0 = dLo + (dHi - dLo) * static_cast<double>(pos);

    g.readIdx = static_cast<double>(mHead) - d0;
    g.rate = rate; g.size = static_cast<float>(size); g.age = 0.f;
    // equal-power pan; centre gives unity on both sides
    const float p = 0.5f + (Rand01() - 0.5f) * mS[5];
    g.gl = 1.41421356f * std::cos(1.5707963f * p);
    g.gr = 1.41421356f * std::sin(1.5707963f * p);
    g.active = true;
  }

  void UpdateViz()
  {
    int n = 0;
    const double head = static_cast<double>(mHead);
    for (int i = 0; i < kMaxGrains; i++)
    {
      const Grain& g = mGrains[i];
      if (!g.active) continue;
      const double d = head - g.readIdx;
      mVizPos[n].store(static_cast<float>(1.0 - d / mUsable), std::memory_order_relaxed);   // 1 = newest (right)
      mVizAmp[n].store(WindowAt(g.age / g.size), std::memory_order_relaxed);
      n++;
    }
    mVizCount.store(n, std::memory_order_relaxed);
  }

  static constexpr int kWin = 512;
  float mSR = 48000.f;
  std::vector<float> mBuf[2];
  long long mMask = 0, mHead = 0;
  double mBufLen = 0.0, mUsable = 0.0;
  Grain mGrains[kMaxGrains];
  CloudReverb mReverb;
  float mP[9] = { 0.5f, 0.4f, 0.5f, 0.5f, 0.3f, 0.5f, 0.f, 0.2f, 0.f };   // position size pitch density texture spread fb reverb mix
  float mS[9] = {};
  float mCoeff = 0.01f, mSpawnAcc = 0.f, mActSmooth = 1.f;
  float mWin[kWin] = {};
  uint32_t mRng = 1u, mVizTick = 0;
  bool mFreeze = false;
  std::atomic<float> mVizPos[kMaxGrains], mVizAmp[kMaxGrains];
  std::atomic<int> mVizCount{0};
};

} // namespace kr106
