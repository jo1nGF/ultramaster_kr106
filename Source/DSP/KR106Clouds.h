#pragma once

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>
#include "KR106CloudsSpectral.h"
#include "KR106CloudsWsola.h"

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

// Firmware names (same order as the Clouds Mode parameter) and the value -> mode mapping
static constexpr int kNumCloudModes = 4;
static constexpr const char* kCloudModeNames[kNumCloudModes] = { "Granular", "Pitch shift", "Looping delay", "Spectral" };
static inline int cloudModeFromValue(float v)
{ return std::max(0, std::min(kNumCloudModes - 1, static_cast<int>(std::round(v * (kNumCloudModes - 1))))); }

// Stereo "Space" reverb for the cloud: a dense, deep tail.
//
//   in -> pre-delay (22 ms) -> 8 input diffusers (smear transients into a cloud)
//      -> tank: 8 damped combs per channel (bigger room than the original, feedback
//         0.93 = long decay, damping 0.42 = dark, warm tail) -> 4 output all-passes
//      -> stereo out
//
// Compared with the first version (see the measurements in the README): longer decay,
// much higher echo density in the first 100 ms, darker high end, same stereo width.
class CloudReverb
{
public:
  // Decay: RT60 from 0.5 s (v = 0) to 8 s (v = 1). Tone: 0 = dark (strong damping), 1 = bright.
  void SetDecay(float v) { mDecayV = std::min(1.f, std::max(0.f, v)); UpdateRoom(); }
  void SetTone(float v)
  {
    v = std::min(1.f, std::max(0.f, v));
    mDamp = 0.7f * (1.f - v);
    UpdateRoom();                                                      // brighter tail rings longer: compensate
  }

  void Init(float sr)
  {
    mSR = sr;
    static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    mDavg = 0.f; for (int i = 0; i < 8; i++) mDavg += combT[i] * kRoomScale * (sr / 44100.f) / 8.f;
    static const int apT[4]   = { 556, 441, 341, 225 };
    static const int dfT[kDiffN] = { 113, 157, 241, 337, 61, 89, 127, 181 };
    const float k = sr / 44100.f;
    mPre.assign(static_cast<size_t>(0.022f * sr) + 2, 0.f);
    mPrePos = 0;
    for (int i = 0; i < kDiffN; i++) { mDiff[i].assign(static_cast<size_t>(dfT[i] * k) + 2, 0.f); mDiffPos[i] = 0; }
    for (int c = 0; c < 2; c++)
    {
      for (int i = 0; i < 8; i++)
      {
        mComb[c][i].assign(static_cast<size_t>(combT[i] * kRoomScale * k) + (c ? 23 : 0) + 2, 0.f);
        mCombPos[c][i] = 0; mCombLP[c][i] = 0.f;
      }
      for (int i = 0; i < 4; i++) { mAp[c][i].assign(static_cast<size_t>(apT[i] * k) + (c ? 23 : 0) + 2, 0.f); mApPos[c][i] = 0; }
    }
  }

  void Process(float in, float& outL, float& outR)
  {
    // pre-delay
    const size_t pp = mPrePos;
    const float d = mPre[pp];
    mPre[pp] = in;
    mPrePos = (pp + 1) % mPre.size();
    float x = d * kIn;

    // input diffusion (all-pass, g = 0.62)
    for (int i = 0; i < kDiffN; i++)
    {
      auto& b = mDiff[i]; size_t& p = mDiffPos[i];
      const float bo = b[p];
      const float y = bo - kDiffG * x;
      b[p] = x + kDiffG * y;
      x = y;
      if (++p >= b.size()) p = 0;
    }

    // tank + output diffusion, per channel
    float o[2];
    for (int c = 0; c < 2; c++)
    {
      const float xc = x;
      float sum = 0.f;
      for (int i = 0; i < 8; i++)
      {
        auto& b = mComb[c][i]; size_t& p = mCombPos[c][i];
        const float y = b[p];
        mCombLP[c][i] = y * (1.f - mDamp) + mCombLP[c][i] * mDamp;
        b[p] = xc + mCombLP[c][i] * mRoom;
        if (++p >= b.size()) p = 0;
        sum += y;
      }
      for (int i = 0; i < 4; i++)
      {
        auto& b = mAp[c][i]; size_t& p = mApPos[c][i];
        const float bo = b[p];
        b[p] = sum + bo * 0.5f;
        sum = bo - sum;
        if (++p >= b.size()) p = 0;
      }
      o[c] = sum * kOut;
    }
    outL = o[0]; outR = o[1];
  }

private:
  static constexpr float kRoomScale = 1.18f;   // bigger room than before -> deeper
  float mSR = 44100.f, mDavg = 1500.f, mDecayV = 0.6f;
  void UpdateRoom()
  {
    // target RT60 0.5 .. 8 s; x1.10 matches the measured tail at tone 0.4;
    // the damping factor removes the extra length a bright tone adds (measured 1.16 at tone 1)
    const float comp = std::min(1.2f, std::max(0.9f, 1.f + 0.38f * (0.42f - mDamp)));
    const float t60 = (0.5f + 29.5f * mDecayV * mDecayV) * 1.10f / comp;
    const float g = std::pow(10.f, -3.f * mDavg / (t60 * mSR));
    mRoom = std::min(0.9995f, std::max(0.5f, g));
  }
  float mRoom = 0.93f;                        // feedback, set from Decay
  float mDamp = 0.42f;                        // damping, set from Tone
  static constexpr int kDiffN = 8;             // input diffusion stages
  static constexpr float kDiffG = 0.66f;       // input diffusion coefficient
  static constexpr float kIn = 0.015f;
  static constexpr float kOut = 2.6f;

  std::vector<float> mPre;
  size_t mPrePos = 0;
  std::vector<float> mDiff[kDiffN];
  size_t mDiffPos[kDiffN] = {};
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
    mReverb.SetDecay(0.3f);
    mReverb.SetTone(0.4f);
    mSpec[0].Init(); mSpec[1].Init(); mWsInit = false;
    // the STFT output is N-1 samples late: the dry path uses the same delay so the mix stays aligned
    mDry[0].assign(CloudSpectral::N - 1, 0.f); mDry[1].assign(CloudSpectral::N - 1, 0.f); mDryPos = 0;
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
  void SetDecay(float v)    { mReverb.SetDecay(v); }
  void SetTone(float v)     { mReverb.SetTone(v); }
  // Firmware: 0 Granular, 1 Pitch shifter / time stretch, 2 Looping delay, 3 Spectral
  void SetMode(int m)       { mMode = std::max(0, std::min(3, m)); }
  int  Mode() const         { return mMode; }
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
    if (mMode == kModeLoop)     { ProcessLoop(inL, inR, outL, outR); return; }
    if (mMode == kModeSpectral) { ProcessSpectral(inL, inR, outL, outR); return; }
    if (mMode == kModePitchShift) { ProcessPitch(inL, inR, outL, outR); return; }
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
  enum { kModeGranular = 0, kModePitchShift = 1, kModeLoop = 2, kModeSpectral = 3 };
  int mMode = 0;
  CloudSpectral mSpec[2];
  std::vector<float> mDry[2];
  size_t mDryPos = 0;
  WsolaPlayer mWs;
  bool mWsInit = false;
  float lpCurrent = 0.f, lpPhase = 0.f, lpLoopPoint = 0.f, lpLoopDur = 0.f, lpTailStart = 0.f, lpTailDur = 1.f, lpLoopReset = 0.f;

  // Looping delay. Port of the Clouds "looping sample player" (Copyright 2014 Emilie Gillet).
  // Permission is hereby granted, free of charge, to any person obtaining a copy of this software
  // and associated documentation files (the "Software"), to deal in the Software without
  // restriction, including without limitation the rights to use, copy, modify, merge, publish,
  // distribute, sublicense, and/or sell copies of the Software, subject to the MIT conditions:
  // the above copyright notice and this permission notice shall be included in all copies or
  // substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
  // Source: github.com/pichenettes/eurorack (clouds/dsp/looping_sample_player.h)
  //
  //  - not frozen: a delay line whose time follows Position (glides smoothly), plus Feedback;
  //  - frozen: the buffer section chosen by Position and Size loops, with Pitch as playback
  //    speed and 64-sample crossfades at each loop point.
  // Clouds' tap-tempo sync is not used here (the module has no trigger input).
  void ProcessLoop(float inL, float inR, float& outL, float& outR)
  {
    for (int i = 0; i < 9; i++) mS[i] += (mP[i] - mS[i]) * mCoeff;
    const double bufLen = static_cast<double>(mMask + 1);
    const double maxDelay = bufLen - 64.0;
    const float in[2] = { inL, inR };
    float wet[2] = { 0.f, 0.f };
    if (!mFreeze)
    {
      const float target = mS[0] * static_cast<float>(maxDelay);
      lpCurrent += 0.00005f * (target - lpCurrent);          // tape-speed glide, as in the source
      const double rp = static_cast<double>(mHead) - 1.0 - static_cast<double>(lpCurrent);
      for (int c = 0; c < 2; c++)
      {
        wet[c] = ReadLinearAt(c, rp);
        mBuf[c][static_cast<size_t>(mHead & mMask)] = in[c] + wet[c] * mS[6] * 0.95f;   // feedback
      }
      mHead++;
    }
    else
    {
      // loop section (source: loop_point = position * max_delay * 15/16 + crossfade;
      // loop_duration = (0.01 + 0.99 size^3) * max_delay)
      const float d = mS[1];
      float loopPoint = mS[0] * static_cast<float>(maxDelay) * 15.f / 16.f + 64.f;
      const float loopDur = (0.01f + 0.99f * d * d * d) * static_cast<float>(maxDelay);
      if (loopPoint + loopDur >= maxDelay) loopPoint = static_cast<float>(maxDelay) - loopDur;
      const float inc = std::pow(2.f, ((mS[2] - 0.5f) * 48.f) / 12.f);
      const float head = static_cast<float>(mHead);   // the head stays put while frozen
      if (lpPhase >= lpLoopDur || lpPhase == 0.f)
      {
        if (lpPhase >= lpLoopDur) lpLoopReset = lpLoopDur;
        if (lpLoopReset >= lpLoopDur) lpLoopReset = lpLoopDur;
        lpTailStart = lpLoopDur - lpLoopReset + lpLoopPoint;
        lpPhase = 0.f;
        lpTailDur = std::min(64.f, 64.f * inc);
        lpLoopPoint = loopPoint;
        lpLoopDur = loopDur;
      }
      lpPhase += inc;
      float gain = 1.f;
      if (lpTailDur != 0.f) gain = std::min(1.f, std::max(0.f, lpPhase / lpTailDur));
      for (int c = 0; c < 2; c++)
      {
        wet[c] = ReadLinearAt(c, static_cast<double>(head) - 1.0 - static_cast<double>(lpLoopDur - lpPhase + lpLoopPoint)) * gain;
        if (gain != 1.f)
          wet[c] += ReadLinearAt(c, static_cast<double>(head) - 1.0 - static_cast<double>(-lpPhase + lpTailStart)) * (1.f - gain);
      }
    }
    ApplyReverbAndMix(wet[0], wet[1], inL, inR, outL, outR);
  }

  // Linear read of channel c at an absolute (fractional) buffer position
  float ReadLinearAt(int c, double pos) const
  {
    const long long i0 = static_cast<long long>(std::floor(pos));
    const float f = static_cast<float>(pos - static_cast<double>(i0));
    const float a = mBuf[c][static_cast<size_t>(i0 & mMask)];
    const float b = mBuf[c][static_cast<size_t>((i0 + 1) & mMask)];
    return a + (b - a) * f;
  }

  // Pitch shift / time stretch: WSOLA (see KR106CloudsWsola.h). Pitch = +-24 st, Position = how far
  // back in the recording the window looks, Size = window width. Freeze stops the recording.
  void ProcessPitch(float inL, float inR, float& outL, float& outR)
  {
    for (int i = 0; i < 9; i++) mS[i] += (mP[i] - mS[i]) * mCoeff;
    if (!mWsInit) { mWs.Init(mMask + 1); mWsInit = true; }
    const float pitch = (mS[2] - 0.5f) * 48.f;
    float wl = 0.f, wr = 0.f;
    mWs.Tick(mBuf[0].data(), mBuf[1].data(), mMask, mHead, pitch, mS[0], mS[1], wl, wr);
    if (!mFreeze)
    {
      mBuf[0][static_cast<size_t>(mHead & mMask)] = inL + wl * mS[6] * 0.95f;   // feedback
      mBuf[1][static_cast<size_t>(mHead & mMask)] = inR + wr * mS[6] * 0.95f;
      mHead++;
    }
    ApplyReverbAndMix(wl, wr, inL, inR, outL, outR);
  }

  // Spectral: STFT stage with a dry path delayed by the same latency.
  void ProcessSpectral(float inL, float inR, float& outL, float& outR)
  {
    for (int i = 0; i < 9; i++) mS[i] += (mP[i] - mS[i]) * mCoeff;
    const float semis = (mS[2] - 0.5f) * 48.f;
    const float ratio = std::pow(2.f, semis / 12.f);
    float wet[2];
    wet[0] = mSpec[0].Process(inL, ratio, mS[4], mS[6], mFreeze);
    wet[1] = mSpec[1].Process(inR, ratio, mS[4], mS[6], mFreeze);
    const size_t dp = mDryPos;
    const float dryL = mDry[0][dp], dryR = mDry[1][dp];
    mDry[0][dp] = inL; mDry[1][dp] = inR;
    mDryPos = (dp + 1) % mDry[0].size();
    ApplyReverbAndMix(wet[0], wet[1], dryL, dryR, outL, outR);
  }

  // Common tail for the non-granular modes: reverb send, mix
  void ApplyReverbAndMix(float gl, float gr, float dryL, float dryR, float& outL, float& outR)
  {
    float rl, rr;
    mReverb.Process(0.5f * (gl + gr), rl, rr);
    const float rev = mS[7];
    const float wl = WetLimit(gl * (1.f - 0.4f * rev) + rl * rev);
    const float wr = WetLimit(gr * (1.f - 0.4f * rev) + rr * rev);
    const float mix = mS[8];
    outL = dryL * (1.f - mix) + wl * mix;
    outR = dryR * (1.f - mix) + wr * mix;
  }
  std::atomic<float> mVizPos[kMaxGrains], mVizAmp[kMaxGrains];
  std::atomic<int> mVizCount{0};
};

} // namespace kr106
