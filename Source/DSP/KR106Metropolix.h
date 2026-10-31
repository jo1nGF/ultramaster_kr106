#pragma once

// Step sequencer modelled on the Intellijel Metropolix (manual v1.6).
//
//   * 8 stages with three panel-wide controls: PITCH sliders, PULSE COUNT (1-8 clock pulses per stage)
//     and GATE TYPE (hold / multi / single / rest).
//   * 2 note tracks (TRK 1, TRK 2) read the same stages in their own way: each has its own play ORDER
//     (20 orders, forward / reverse), length (stages, offset, pulses), clock divider, pulse-count divider,
//     swing, slide, gate length, transposition, slider range, gate stretching, ratchet mode ...
//   * Per stage and per track: SLIDE, SKIP, PITCH override, GATE override, RATCHets (1-8), PROBability
//     (per stage or per pulse), ACCUMulator (cumulative transposition in scale degrees, with limits) and a
//     CV lane (voltage + step / stage ramp / pulse ramp).
//   * 8 MOD lanes of 8 stages with their own order, length and clock divider.
//   * Scale + root quantizing (the plugin's scale list), 64 presets (8 banks of 8).
//
// The note tracks play notes in the voices. The CV lanes of both tracks and the 8 MOD lanes come out as
// modulation sources, and the slide of the playing stage comes out as mGlideOut (it opens up Porta).
// Everything the screen edits is read by the audio thread through atomics.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include "KR106Varigate.h"      // A8
#include "KR106Scale.h"

namespace kr106 {

static constexpr int kMxTracks = 2, kMxStages = 8, kMxLanes = 8, kMxPresets = 64, kMxOrders = 20;

static constexpr const char* kMxOrderNames[kMxOrders] = {
  "Linear", "Ping Pong", "Pendulum", "Odd/Even", "Jump by 2", "Jump by 3", "Jump by 4", "Jump by 5", "Hopscotch",
  "Arp 3 Shift -1", "Arp 4 Shift -2", "Arp 4 Shift -1", "Converge", "Diverge", "Growing", "Shrinking",
  "Pedal Point", "Brownian", "Shuffle", "Random" };
enum { kMxOrdBrownian = 17, kMxOrdShuffle = 18, kMxOrdRandom = 19 };

enum MxGateType { kMxHold = 0, kMxMulti, kMxSingle, kMxRest };
static constexpr const char* kMxGateTypeNames[4] = { "HOLD", "MULTI", "SINGLE", "REST" };
static constexpr const char* kMxGateTypeOvr[7] = { "Switch", "Single", "Sngl+Rst", "Multi", "Multi+Rst", "Hold", "Hold+Rst" };
static constexpr const char* kMxSlideTypes[3] = { "Analog", "Tempo", "Acid" };
static constexpr const char* kMxRatchModes[3] = { "Multiply", "Pulse", "Gated" };

// What a CTRL knob, an AUX input (X, Y, Z) or a MOD lane can control. kind: 0 = unipolar (lo..hi), 1 = bipolar
// (+- hi around the panel value), 2 = gate / trigger (high above 30 %).
enum MxDest {
  kDNone = 0, kDBpm, kDClockDiv, kDDirection, kDGateLen, kDGateScale, kDOctave, kDPitchPre, kDPitchPost, kDOrder, kDProb,
  kDPulseCount, kDPulseDiv, kDPulsesLen, kDRatchet, kDRoot, kDSlideAmt, kDSliderRange, kDStagesLen, kDStageOfs, kDSwing,
  kDMute, kDReset, kDSkipInvert, kDSlide, kDAccum, kDAccumReset, kMxNumDest };
struct MxDestInfo { const char* name; int kind, lo, hi; };
static constexpr MxDestInfo kMxDests[kMxNumDest] = {
  { "None", 0, 0, 0 },              { "BPM", 0, 30, 300 },             { "Clock Div", 0, 1, 16 },       { "Direction (gate)", 2, 0, 0 },
  { "Gate Length", 0, 1, 100 },     { "Gate Scale", 0, 1, 100 },       { "Octave +-", 1, 0, 2 },        { "Pitch Pre +-", 1, 0, 7 },
  { "Pitch Post +-", 1, 0, 12 },    { "Play Order", 0, 0, 19 },        { "Probability +-", 1, 0, 100 }, { "Pulse Count +-", 1, 0, 7 },
  { "Pulse Div", 0, 1, 8 },         { "Pulses Len", 0, 0, 64 },        { "Ratchet +-", 1, 0, 7 },       { "Root +-", 1, 0, 11 },
  { "Slide Amount", 0, 0, 100 },    { "Slider Range", 0, 1, 4 },       { "Stages Len", 0, 1, 8 },       { "Stage Offset", 0, 0, 7 },
  { "Swing", 0, 50, 78 },           { "Mute (gate)", 2, 0, 0 },        { "Reset (gate)", 2, 0, 0 },     { "Skip Invert (gate)", 2, 0, 0 },
  { "Slide (gate)", 2, 0, 0 },      { "Accumulate (gate)", 2, 0, 0 },  { "Accum Reset (gate)", 2, 0, 0 } };
static constexpr const char* kMxAssignNames[3] = { "TRK 1", "TRK 2", "TRK 1+2" };       // mask 1, 2, 3
static constexpr const char* kMxAuxSources[5] = { "None", "LFO 1", "LFO 2", "LFO 3", "Envelope" };
static constexpr const char* kMxOutNames[15] = { "None", "Trk 1 Pitch", "Trk 2 Pitch", "Trk 1 Gate", "Trk 2 Gate", "Trk 1 CV", "Trk 2 CV",
  "MOD 1", "MOD 2", "MOD 3", "MOD 4", "MOD 5", "MOD 6", "MOD 7", "MOD 8" };

static inline int s8(const A8& a) { return static_cast<int>(a.get()) - 128; }
static inline void s8set(A8& a, int v) { a.set(std::max(-128, std::min(127, v)) + 128); }

struct MxStage
{
  A8 skip, slide, pitchOvr, gateOvr, ratch, prob, probTarget, accAmt, accTrig, cvVal, cvRamp, cvGate;
  // pitchOvr: 0 = none, else note + 1 (C0..C5 = 1..61). gateOvr: 0 = '--', 1 = rest, 2..101 = 1..100 %.
  // accAmt / cvVal are signed (s8). accTrig: 0 stage, 1 pulse, 2 ratchet. probTarget: 0 stage, 1 pulse.
  template <class F> void each(F&& f)
  { f(skip); f(slide); f(pitchOvr); f(gateOvr); f(ratch); f(prob); f(probTarget); f(accAmt); f(accTrig); f(cvVal); f(cvRamp); f(cvGate); }
  void Init()
  {
    skip.set(0); slide.set(0); pitchOvr.set(0); gateOvr.set(0); ratch.set(1); prob.set(100); probTarget.set(0);
    s8set(accAmt, 0); accTrig.set(0); s8set(cvVal, 0); cvRamp.set(0); cvGate.set(0);
  }
};

struct MxAssign
{
  A8 dest, mask;                                   // mask: 1 = TRK 1, 2 = TRK 2, 3 = both
  template <class F> void each(F&& f) { f(dest); f(mask); }
  void Init() { dest.set(0); mask.set(3); }
};

struct MxTrack
{
  MxStage st[kMxStages];
  A8 order, dir, stages, offset, pulses, clkDiv, pulseDiv, swing, slideAmt, slideType, gateLen, gateScale,
     transpose, sliderOct, sliderDir, pulseOvr, gateTypeOvr, stretch, ratchMode, restPitch, mute,
     limPos, limNeg, accMode, accOrder, accPolar, cvType;      // cvType (since state v2): 0 = CV value, 1 = gate toggle
  // pulses: 0 = Auto. swing 50..78. slideAmt 0..100. gateLen 1..99, 100 = hold. gateScale 1..100.
  // transpose signed (s8). sliderOct 1..4. pulseOvr 0 = use switches. stretch: gate stretching on/off.
  // accMode: 0 stage, 1 track. accOrder: 0 wrap, 1 pendulum. accPolar: 0 unipolar, 1 bipolar.
  template <class F> void each(F&& f, int ver = 2)
  {
    for (auto& s : st) s.each(f);
    if (ver >= 2) f(cvType);
    f(order); f(dir); f(stages); f(offset); f(pulses); f(clkDiv); f(pulseDiv); f(swing); f(slideAmt); f(slideType); f(gateLen);
    f(gateScale); f(transpose); f(sliderOct); f(sliderDir); f(pulseOvr); f(gateTypeOvr); f(stretch); f(ratchMode); f(restPitch);
    f(mute); f(limPos); f(limNeg); f(accMode); f(accOrder); f(accPolar);
  }
  void InitSettings()
  {
    order.set(0); dir.set(0); stages.set(8); offset.set(0); pulses.set(0); clkDiv.set(1); pulseDiv.set(1); swing.set(50);
    slideAmt.set(30); slideType.set(0); gateLen.set(50); gateScale.set(100); s8set(transpose, 0); sliderOct.set(2); sliderDir.set(0);
    pulseOvr.set(0); gateTypeOvr.set(0); stretch.set(0); ratchMode.set(0); restPitch.set(0); mute.set(0);
    limPos.set(7); limNeg.set(7); accMode.set(0); accOrder.set(0); accPolar.set(0); cvType.set(0);
  }
  void Init() { for (auto& s : st) s.Init(); InitSettings(); }
};

struct MxLane
{
  A8 val[kMxStages], ramp[kMxStages], gate[kMxStages];
  A8 order, dir, stages, offset, clkDiv, type, mute;      // type: 0 bipolar, 1 unipolar, 2 gate toggles
  A8 dest, mask;                                          // what the lane controls (since state v2)
  template <class F> void each(F&& f, int ver = 2)
  {
    if (ver >= 2) { f(dest); f(mask); }
    for (auto& a : val) f(a);
    for (auto& a : ramp) f(a);
    for (auto& a : gate) f(a);
    f(order); f(dir); f(stages); f(offset); f(clkDiv); f(type); f(mute);
  }
  void Init()
  {
    for (int i = 0; i < kMxStages; i++) { s8set(val[i], 0); ramp[i].set(0); gate[i].set(0); }
    order.set(0); dir.set(0); stages.set(8); offset.set(0); clkDiv.set(1); type.set(0); mute.set(0); dest.set(0); mask.set(3);
  }
};

struct MxPattern
{
  A8 pitch[kMxStages], pulse[kMxStages], gtype[kMxStages];     // the panel: sliders / switches
  MxTrack trk[kMxTracks];
  MxLane lane[kMxLanes];
  A8 root, scaleType, scaleLo, scaleHi;                        // scaleType: index into kScaleDefs, scaleLo/Hi: custom mask
  MxAssign ctrl[2], aux[3];                                    // CTRL 1-2 and AUX X-Z assignments (since state v2)
  A8 auxSrc[3], auxAmt[3];                                     // what feeds an AUX input (0 none, 1-3 LFO, 4 envelope) and its attenuverter (s8)
  A8 outA, outB;                                               // what OUT A / OUT B give (kMxOutNames)

  template <class F> void each(F&& f, int ver = 2)
  {
    for (auto& a : pitch) f(a);
    for (auto& a : pulse) f(a);
    for (auto& a : gtype) f(a);
    for (auto& t : trk) t.each(f, ver);
    for (auto& l : lane) l.each(f, ver);
    f(root); f(scaleType); f(scaleLo); f(scaleHi);
    if (ver >= 2)
    {
      for (auto& c : ctrl) c.each(f);
      for (auto& a : aux) a.each(f);
      for (auto& a : auxSrc) f(a);
      for (auto& a : auxAmt) f(a);
      f(outA); f(outB);
    }
  }
  uint16_t custom() const { return static_cast<uint16_t>(scaleLo.get() | ((scaleHi.get() & 0x0F) << 8)); }
  void Init()
  {
    for (int i = 0; i < kMxStages; i++) { pitch[i].set(i * 127 / 7 / 2); pulse[i].set(1); gtype[i].set(kMxSingle); }
    for (auto& t : trk) t.Init();
    for (auto& l : lane) l.Init();
    root.set(0); scaleType.set(1); scaleLo.set(0xB5); scaleHi.set(0x0A);        // C major
    for (auto& c : ctrl) c.Init();
    for (auto& a : aux) a.Init();
    for (int i = 0; i < 3; i++) { auxSrc[i].set(0); s8set(auxAmt[i], 127); }
    outA.set(1); outB.set(2);
  }
};

// ---- orders -------------------------------------------------------------------------------------
// Fills `out` with the stage indices (0..N-1) of one cycle of a deterministic order. Reverse = every index
// mirrored (i -> N-1-i), which is what the manual shows for every order.
static inline void mxBuildOrder(int order, int N, int dir, std::vector<uint8_t>& out)
{
  out.clear();
  N = std::max(1, std::min(8, N));
  auto push = [&](int v) { out.push_back(static_cast<uint8_t>(((v % N) + N) % N)); };
  switch (order)
  {
    case 0: for (int i = 0; i < N; i++) push(i); break;
    case 1: for (int i = 0; i < N; i++) push(i); for (int i = N - 1; i >= 0; i--) push(i); break;
    case 2: for (int i = 0; i < N; i++) push(i); for (int i = N - 2; i >= 1; i--) push(i); break;
    case 3: for (int i = 0; i < N; i += 2) push(i); for (int i = 1; i < N; i += 2) push(i); break;
    case 4: case 5: case 6: case 7:
    {
      const int j = order - 2;                                   // jump by 2, 3, 4, 5
      int p = 0;
      do { push(p); p = (p + j) % N; } while (p != 0 && static_cast<int>(out.size()) < 64);
      break;
    }
    case 8: for (int k = 0; k < N; k++) { push(k); push(k + 2); } break;
    case 9: for (int k = 0; k < N; k++) { push(k); push(k + 1); push(k + 2); } break;
    case 10: for (int k = 0; k < N; k++) for (int j = 0; j < 4; j++) push(k + j); break;
    case 11: for (int k = 0; k < N; k += 2) for (int j = 0; j < 4; j++) push(k + j); break;
    case 12:
    {
      int lo = 0, hi = N - 1;
      while (lo <= hi) { push(lo++); if (lo <= hi) push(hi--); }
      break;
    }
    case 13:
    {
      int lo = (N - 1) / 2, hi = lo + 1;
      while (lo >= 0 || hi < N) { if (lo >= 0) out.push_back(static_cast<uint8_t>(lo--)); if (hi < N) out.push_back(static_cast<uint8_t>(hi++)); }
      break;
    }
    case 14: for (int m = 1; m <= N; m++) for (int i = 0; i < m; i++) push(i); break;
    case 15: for (int m = N; m >= 1; m--) for (int i = 0; i < m; i++) push(i); break;
    case 16: for (int k = 1; k < N; k++) { push(0); push(k); } if (N == 1) push(0); break;
    default: for (int i = 0; i < N; i++) push(i); break;
  }
  if (dir) for (auto& v : out) v = static_cast<uint8_t>(N - 1 - v);
}

// ---------------------------------------------------------------------------------------------------
struct MxGate { float start, len; uint8_t flags; };          // in pulses from the start of the stage; flags: 1 = pulse start, 2 = stage start

class MetropolixSeq
{
public:
  MxPattern mPat;
  std::vector<MxPattern> mPresets;                          // 64

  std::atomic<bool> mRun { false }, mSync { false }, mResetReq { false };
  std::atomic<float> mBpm { 120.f };
  std::atomic<int> mBank { 0 }, mSlot { 0 };
  std::atomic<int8_t> mLoopA[kMxTracks], mLoopB[kMxTracks];        // loopy: first / last stage held (-1 = none)
  std::atomic<int> mLoopPulses { 0 };                              // 0 = Auto
  std::atomic<int8_t> mAudition[kMxTracks];                        // stage player (-1 = none)
  std::atomic<uint8_t> mLaneMute[kMxLanes];
  double mHostBPM = 120.0;
  bool mHostPlaying = false;

  // read by the screen and the modulation matrix
  std::atomic<int8_t> mPos[kMxTracks];                             // playing stage (-1 = none)
  std::atomic<uint8_t> mHigh[kMxTracks];                           // gate high
  std::atomic<uint8_t> mPulseLed[kMxTracks];                       // pulse index inside the stage + 1
  std::atomic<float> mLaneOut[kMxLanes];                           // -1..1
  std::atomic<float> mCvOut[kMxTracks];                            // -1..1
  std::atomic<float> mGlideOut { 0.f };
  std::atomic<uint8_t> mClkOut { 0 };
  std::atomic<int> mNote[kMxTracks];                               // note that sounds (-1 none)
  std::atomic<uint8_t> mLoopLed[kMxTracks];
  std::atomic<float> mCtrl[2];                                     // CTRL 1 / 2 knobs, 0..1 (12 o'clock = 0.5)
  std::atomic<float> mAuxIn[3];                                    // AUX X / Y / Z inputs after the attenuverter, -1..1
  std::atomic<float> mOutA { 0.f }, mOutB { 0.f };                 // OUT A / B, -1..1

  MetropolixSeq()
  {
    mPat.Init();
    mPresets.resize(kMxPresets);
    for (auto& p : mPresets) p.Init();
    InitDemo(mPresets[0]);
    mPat = mPresets[0];
    for (int t = 0; t < kMxTracks; t++) { mLoopA[t].store(-1); mLoopB[t].store(-1); mAudition[t].store(-1); mPos[t].store(-1); mHigh[t].store(0); mPulseLed[t].store(0); mCvOut[t].store(0.f); mNote[t].store(-1); mLoopLed[t].store(0); }
    for (int l = 0; l < kMxLanes; l++) { mLaneOut[l].store(0.f); mLaneMute[l].store(0); }
    for (auto& c : mCtrl) c.store(0.5f);
    for (auto& a : mAuxIn) a.store(0.f);
    mAll.reserve(160); mOrdTmp.reserve(128);
    RefreshScale();
  }

  void SetSampleRate(float sr) { mSampleRate = static_cast<double>(sr); }

  static void InitDemo(MxPattern& p)
  {
    p.Init();
    const int pitches[8] = { 0, 4, 7, 12, 7, 4, 9, 5 };             // semitones over the root; written as slider values below
    for (int i = 0; i < 8; i++) p.pitch[i].set(std::min(127, pitches[i] * 127 / 24));
    for (int i = 0; i < 8; i++) { p.pulse[i].set(i == 3 ? 2 : 1); p.gtype[i].set(i == 6 ? kMxRest : kMxSingle); }
    p.trk[1].order.set(2);                                              // track 2: pendulum, half speed, an octave up
    p.trk[1].clkDiv.set(2); s8set(p.trk[1].transpose, 12); p.trk[1].gateLen.set(30);
  }

  // ---- user actions -----------------------------------------------------------------------------
  void Recall(int preset)
  {
    preset = std::max(0, std::min(kMxPresets - 1, preset));
    mPat = mPresets[preset];
    mBank.store(preset / 8); mSlot.store(preset % 8);
  }
  void Save(int preset) { mPresets[std::max(0, std::min(kMxPresets - 1, preset))] = mPat; }
  void InitPreset() { mPat.Init(); }
  void RequestReset() { mResetReq.store(true); }
  void SetLoop(int t, int a, int b) { mLoopA[t].store(static_cast<int8_t>(a)); mLoopB[t].store(static_cast<int8_t>(b)); }

  // ---- state ------------------------------------------------------------------------------------
  static constexpr uint8_t kStateVersion = 2;
  void Serialize(std::vector<uint8_t>& out) const
  {
    out.push_back(kStateVersion);
    Put(out, mPat);
    for (auto& p : mPresets) Put(out, p);
    out.push_back(static_cast<uint8_t>(mBank.load()));
    out.push_back(static_cast<uint8_t>(mSlot.load()));
    const float bpm = mBpm.load();
    const uint8_t* bp = reinterpret_cast<const uint8_t*>(&bpm);
    for (int i = 0; i < 4; i++) out.push_back(bp[i]);
    out.push_back(mSync.load() ? 1 : 0);
    out.push_back(static_cast<uint8_t>(mLoopPulses.load()));
  }
  bool Deserialize(const uint8_t* d, size_t n)
  {
    if (d == nullptr || n < 1 || (d[0] != 1 && d[0] != kStateVersion)) return false;
    const int ver = d[0];
    const size_t pat = PatternBytes(ver);
    if (n < 1 + pat * (1 + kMxPresets) + 2 + 4 + 2) return false;
    size_t o = 1;
    Get(d + o, mPat, ver); o += pat;
    for (auto& p : mPresets) { Get(d + o, p, ver); o += pat; }
    mBank.store(std::min<int>(7, d[o++]));
    mSlot.store(std::min<int>(7, d[o++]));
    float bpm; std::copy(d + o, d + o + 4, reinterpret_cast<uint8_t*>(&bpm)); o += 4;
    mBpm.store(std::max(20.f, std::min(400.f, bpm)));
    mSync.store(d[o++] != 0);
    mLoopPulses.store(std::min<int>(64, d[o++]));
    return true;
  }

  // ---- note helpers (also used by the screen) ----------------------------------------------------
  // the in-scale notes of the current scale, over the whole MIDI range
  void RefreshScale()
  {
    const uint16_t mask = scaleAbsMask(mPat.scaleType.get(), RootEff(), mPat.custom());
    if (mask == mScaleMaskCached && !mAll.empty()) return;
    mScaleMaskCached = mask;
    mAll.clear();
    for (int n = 0; n < 128; n++) if (mask & (1u << (n % 12))) mAll.push_back(n);
    if (mAll.empty()) mAll.push_back(60);
  }
  static constexpr int kBaseNote = 36;                               // bottom of the pitch sliders (C2, or the root)
  int SliderNote(int t, int stage) const
  {
    const MxTrack& T = mPat.trk[t];
    const int oct = std::max(1, std::min(4, MI(t, kDSliderRange, T.sliderOct.get())));
    const int bottom = kBaseNote + RootEff();
    int pool[64], n = 0;
    for (int note : mAll) if (note >= bottom && note <= bottom + 12 * oct && n < 64) pool[n++] = note;
    if (n == 0) return bottom;
    int idx = static_cast<int>(std::lround(mPat.pitch[stage].get() / 127.0 * (n - 1)));
    if (T.sliderDir.get()) idx = n - 1 - idx;
    return pool[std::max(0, std::min(n - 1, idx))];
  }
  int ShiftDegrees(int note, int deg) const
  {
    if (deg == 0 || mAll.empty()) return note;
    int idx = static_cast<int>(std::lower_bound(mAll.begin(), mAll.end(), note) - mAll.begin());
    if (idx >= static_cast<int>(mAll.size())) idx = static_cast<int>(mAll.size()) - 1;
    if (idx > 0 && std::abs(mAll[idx - 1] - note) <= std::abs(mAll[idx] - note)) idx--;
    idx = std::max(0, std::min(static_cast<int>(mAll.size()) - 1, idx + deg));
    return mAll[idx];
  }
  int NoteForStage(int t, int stage, int accDeg) const
  {
    const MxTrack& T = mPat.trk[t];
    const MxStage& S = T.st[stage];
    int note = S.pitchOvr.get() ? ShiftDegrees(24 + S.pitchOvr.get() - 1, 0) : SliderNote(t, stage);
    if (S.pitchOvr.get()) note = Nearest(note);
    note = ShiftDegrees(note, accDeg + MI(t, kDPitchPre, 0));
    return std::max(0, std::min(127, note + s8(T.transpose) + 12 * MI(t, kDOctave, 0) + MI(t, kDPitchPost, 0)));
  }

  // ---- audio ------------------------------------------------------------------------------------
  void Reset()
  {
    for (auto& c : mT) c = TrkRt();
    for (auto& l : mL) l = LaneRt();
    for (int t = 0; t < kMxTracks; t++) { mPos[t].store(-1); mHigh[t].store(0); mNote[t].store(-1); mLoopLed[t].store(0); mPulseLed[t].store(0); }
    mGlideOut.store(0.f); mClkOut.store(0); mWasRun = false; mClkPhase = 0.0;
  }
  void ForgetNotes() { for (auto& c : mT) { c.sounding = -1; c.high = false; c.audNote = -1; } mGlideOut.store(0.f); for (auto& n : mNote) n.store(-1); }

  template <typename NoteOffF>
  void StopAll(NoteOffF noteOff)
  {
    for (auto& c : mT) { if (c.sounding >= 0) noteOff(c.sounding, 0); if (c.audNote >= 0) noteOff(c.audNote, 0); }
    Reset();
  }

  template <typename NoteOnF, typename NoteOffF>
  void Process(int nFrames, NoteOnF noteOn, NoteOffF noteOff)
  {
    UpdateMods();
    RefreshScale();
    const bool sync = mSync.load();
    const bool run = sync ? mHostPlaying : mRun.load();
    Audition(noteOn, noteOff);
    if (mResetReq.exchange(false)) ResetPositions(noteOff);
    if (!run)
    {
      if (mWasRun) { ResetPositions(noteOff); mWasRun = false; }
      Publish();
      return;
    }
    if (!mWasRun)
    {
      ResetPositions(noteOff); mWasRun = true;
      for (int l = 0; l < kMxLanes; l++) StepLane(l, 0.0);         // the lanes already have a value when the first note plays
      UpdateMods();
    }
    ProcessTriggers(noteOff);

    double bpm = sync ? mHostBPM : static_cast<double>(mBpm.load());
    if (mM[0].abs[kDBpm] || mM[0].ofs[kDBpm] != 0.f) bpm = MIg(kDBpm, static_cast<int>(std::lround(bpm)));
    const double inc = std::max(1.0, bpm) * 4.0 / 60.0 / std::max(1.0, mSampleRate);       // 16th-note pulses per sample
    for (int s = 0; s < nFrames; s++)
    {
      mClkPhase += inc;
      if (mClkPhase >= 1.0) mClkPhase -= 1.0;
      for (int t = 0; t < kMxTracks; t++) StepTrack(t, inc, s, noteOn, noteOff);
      for (int l = 0; l < kMxLanes; l++) StepLane(l, inc);
    }
    Publish();
  }

private:
  struct Runner
  {
    bool started = false;
    int seqPos = -1, bpos = 0, stage = 0, pulseInStage = 0, pulseCount = 1, totalPulses = 0, seqKey = -1;
    std::vector<uint8_t> seq;
    // plan of the stage that plays
    MxGate gates[64];
    int8_t decided[64];
    int nGates = 0, gi = 0;
    bool tie = false, pulseOK = true, fire = true;
    uint32_t serial = 0;
    std::vector<uint8_t> loopList;
    Runner() { seq.reserve(160); loopList.reserve(16); for (auto& d : decided) d = -1; }
  };
  struct TrkRt
  {
    double ph = 1.0;
    long pulseIdx = 0;
    Runner mainR, loopR;
    bool loopActive = false, loopArm = false, loopDisarm = false;
    bool high = false, prevTie = false;
    uint32_t lastSerial = 0;
    int lastGi = -1, sounding = -1, audNote = -1, lastAud = -1, lastNote = -1;
    int accDeg[kMxStages] = {}, accDir[kMxStages] = {}, accTrk = 0, accTrkDir = 1;
    float glide = 0.f;
    float cvPrev = 0.f, cvCur = 0.f;
    int cvPrevStage = 0;
  };
  struct LaneRt { double ph = 1.0; Runner r; float prevVal = 0.f, curVal = 0.f; int stage = 0; float out = 0.f; };

  TrkRt mT[kMxTracks];
  LaneRt mL[kMxLanes];
  std::vector<int> mAll;
  std::vector<uint8_t> mOrdTmp;
  uint16_t mScaleMaskCached = 0xFFFF;
  double mSampleRate = 44100.0, mClkPhase = 0.0;
  bool mWasRun = false;
  uint32_t mRng = 0x1F123BB5u, mSerial = 0;

  uint32_t NextRand() { mRng ^= mRng << 13; mRng ^= mRng >> 17; mRng ^= mRng << 5; return mRng; }
  double Rand01() { return static_cast<double>(NextRand() >> 8) / 16777216.0; }
  int Nearest(int note) const
  {
    int best = mAll.empty() ? note : mAll[0];
    for (int n : mAll) if (std::abs(n - note) < std::abs(best - note)) best = n;
    return best;
  }

  // ---- CTRL knobs, AUX inputs and MOD lanes ------------------------------------------------------
  // Every assignment (dest, tracks) feeds one of these per-track tables. A CTRL knob sets the value
  // directly (abs), AUX and MOD lanes add an offset (ofs). Gate destinations are high above 30 %.
  struct ModState { bool abs[kMxNumDest], gate[kMxNumDest], prevGate[kMxNumDest]; float absV[kMxNumDest], ofs[kMxNumDest]; };
  ModState mM[kMxTracks] = {};
  int mRootOfs = 0;

  int RootEff() const { return ((mPat.root.get() + mRootOfs) % 12 + 12) % 12; }

  int MI(int t, int d, int base) const
  {
    if (d <= 0 || d >= kMxNumDest) return base;
    const MxDestInfo& i = kMxDests[d];
    const ModState& m = mM[t];
    int v = base;
    if (m.abs[d]) v = (i.kind == 1) ? base + static_cast<int>(std::lround((m.absV[d] * 2.f - 1.f) * i.hi))
                                    : i.lo + static_cast<int>(std::lround(m.absV[d] * static_cast<float>(i.hi - i.lo)));
    if (m.ofs[d] != 0.f) v += static_cast<int>(std::lround(m.ofs[d] * (i.kind == 1 ? static_cast<float>(i.hi) : (i.hi - i.lo) * 0.5f)));
    if (i.kind == 0 && i.hi > i.lo) v = std::max(i.lo, std::min(i.hi, v));
    return v;
  }
  int MIg(int d, int base) const { return MI(0, d, base); }            // global destinations: both tables hold the same values
  bool MG(int t, int d) const { return mM[t].gate[d]; }
  bool MRise(int t, int d) const { return mM[t].gate[d] && !mM[t].prevGate[d]; }
  int EffProb(int t, const MxStage& S) const { return std::max(0, std::min(100, static_cast<int>(S.prob.get()) + MI(t, kDProb, 0))); }

  void UpdateMods()
  {
    for (auto& m : mM)
      for (int d = 0; d < kMxNumDest; d++) { m.prevGate[d] = m.gate[d]; m.abs[d] = false; m.absV[d] = 0.f; m.ofs[d] = 0.f; }
    auto apply = [&](int dest, int mask, bool isAbs, float v) {
      if (dest <= 0 || dest >= kMxNumDest) return;
      const bool global = (dest == kDBpm || dest == kDRoot);
      for (int t = 0; t < kMxTracks; t++)
        if (global || (mask & (1 << t)))
        {
          if (isAbs) { mM[t].abs[dest] = true; mM[t].absV[dest] = v; } else mM[t].ofs[dest] += v;
        }
    };
    for (int i = 0; i < 2; i++) apply(mPat.ctrl[i].dest.get(), mPat.ctrl[i].mask.get(), true, mCtrl[i].load());
    for (int i = 0; i < 3; i++) apply(mPat.aux[i].dest.get(), mPat.aux[i].mask.get(), false, mAuxIn[i].load());
    for (int l = 0; l < kMxLanes; l++) apply(mPat.lane[l].dest.get(), mPat.lane[l].mask.get(), false, mL[l].out);
    for (auto& m : mM)
      for (int d = 1; d < kMxNumDest; d++)
        m.gate[d] = (kMxDests[d].kind == 2) && ((m.abs[d] ? m.absV[d] > 0.5f : m.ofs[d] > 0.3f));
    mRootOfs = mM[0].abs[kDRoot] || mM[0].ofs[kDRoot] != 0.f ? MIg(kDRoot, 0) : 0;
  }

  template <typename NoteOffF>
  void ProcessTriggers(NoteOffF noteOff)
  {
    for (int t = 0; t < kMxTracks; t++)
    {
      if (MRise(t, kDReset)) ResetTrackNow(t, noteOff);
      if (MRise(t, kDAccumReset)) ResetAccum(t);
      if (MRise(t, kDAccum))
        for (int st = 0; st < kMxStages; st++) Accumulate(t, st, 0, true);
    }
  }

  // ---- stage order -----------------------------------------------------------------------------
  // next stage index (0..N-1) of the order; `r.seqPos` walks the cycle
  int AdvanceSeq(Runner& r, int order, int dir, int N)
  {
    N = std::max(1, std::min(8, N));
    if (order == kMxOrdRandom) { r.seqPos++; return static_cast<int>(NextRand() % N); }
    if (order == kMxOrdBrownian)
    {
      const double x = Rand01();
      const int step = x < 0.5 ? 1 : (x < 0.75 ? 0 : -1);
      r.bpos = ((r.bpos + (dir ? -step : step)) % N + N) % N;
      r.seqPos++;
      return r.bpos;
    }
    const int key = order | (dir << 5) | (N << 6);
    if (order == kMxOrdShuffle)
    {
      if (r.seqKey != key || r.seqPos < 0 || r.seqPos + 1 >= static_cast<int>(r.seq.size()))
      {
        if (r.seqKey != key || r.seq.empty() || r.seqPos < 0) Shuffle(r, N);
        r.seqKey = key;
        if (r.seqPos + 1 >= static_cast<int>(r.seq.size())) r.seqPos = -1;
      }
      r.seqPos++;
      return r.seq[static_cast<size_t>(r.seqPos)];
    }
    if (r.seqKey != key) { mxBuildOrder(order, N, dir, r.seq); r.seqKey = key; if (r.seqPos >= static_cast<int>(r.seq.size())) r.seqPos = -1; }
    r.seqPos = (r.seqPos + 1) % static_cast<int>(r.seq.size());
    return r.seq[static_cast<size_t>(r.seqPos)];
  }
  void Shuffle(Runner& r, int N)
  {
    r.seq.clear();
    for (int i = 0; i < N; i++) r.seq.push_back(static_cast<uint8_t>(i));
    for (int i = N - 1; i > 0; i--) std::swap(r.seq[static_cast<size_t>(i)], r.seq[NextRand() % static_cast<uint32_t>(i + 1)]);
  }

  // ---- the plan of a stage: its gates -----------------------------------------------------------
  void BuildPlan(int t, int stage, Runner& r)
  {
    const MxTrack& T = mPat.trk[t];
    const MxStage& S = T.st[stage];
    r.stage = stage; r.nGates = 0; r.gi = 0; r.tie = false; r.pulseOK = true; r.fire = true;
    r.serial = ++mSerial;
    for (auto& d : r.decided) d = -1;

    int pc = T.pulseOvr.get() ? T.pulseOvr.get() : mPat.pulse[stage].get();
    pc = std::max(1, std::min(8, pc + MI(t, kDPulseCount, 0)));
    r.pulseCount = pc;

    int gt = std::min<int>(3, mPat.gtype[stage].get());
    switch (T.gateTypeOvr.get())
    {
      case 1: gt = kMxSingle; break;
      case 2: if (gt != kMxRest) gt = kMxSingle; break;
      case 3: gt = kMxMulti; break;
      case 4: if (gt != kMxRest) gt = kMxMulti; break;
      case 5: gt = kMxHold; break;
      case 6: if (gt != kMxRest) gt = kMxHold; break;
      default: break;
    }
    if ((S.slide.get() || MG(t, kDSlide)) && T.slideType.get() == 2 && gt != kMxRest) gt = kMxHold;         // acid slide: the gate stays high

    double gl = std::max(1, std::min(100, MI(t, kDGateLen, T.gateLen.get()))) / 100.0;
    if (S.gateOvr.get() == 1) gt = kMxRest;
    else if (S.gateOvr.get() >= 2) gl = std::max(1, std::min(100, S.gateOvr.get() - 1)) / 100.0;
    gl = std::max(0.01, std::min(1.0, gl * std::max(1, std::min(100, MI(t, kDGateScale, T.gateScale.get()))) / 100.0));
    const bool stretch = T.stretch.get() != 0;

    // probability of the whole stage
    if (EffProb(t, S) < 100 && S.probTarget.get() == 0 && Rand01() >= EffProb(t, S) / 100.0) { r.fire = false; return; }

    struct B { double s, l; } base[8];
    int nb = 0;
    const int div = std::max(1, std::min(8, MI(t, kDPulseDiv, T.pulseDiv.get())));
    switch (gt)
    {
      case kMxHold:   base[nb++] = { 0.0, static_cast<double>(pc) }; break;
      case kMxMulti:  for (int g = 0; g < pc && nb < 8; g += div) { const int len = std::min(div, pc - g); base[nb++] = { static_cast<double>(g), len * gl }; } break;
      case kMxSingle: base[nb++] = { 0.0, stretch ? pc * gl : gl }; break;
      default: break;
    }
    const int rr = std::max(1, std::min(8, static_cast<int>(S.ratch.get()) + MI(t, kDRatchet, 0)));
    const int mode = std::min<int>(2, T.ratchMode.get());
    auto add = [&](double s, double l, int flags) {
      if (r.nGates < 64) { r.gates[r.nGates++] = { static_cast<float>(s), static_cast<float>(l), static_cast<uint8_t>(flags) }; }
    };
    bool firstGate = true;
    for (int b = 0; b < nb; b++)
    {
      const double bs = base[b].s, bl = base[b].l;
      if (rr <= 1)
      {
        add(bs, bl, 1 | (firstGate ? 2 : 0)); firstGate = false;
        continue;
      }
      if (!stretch)                                           // each covered pulse is split into rr gates
      {
        const int k0 = static_cast<int>(std::floor(bs + 1e-9)), k1 = static_cast<int>(std::ceil(bs + bl - 1e-9));
        for (int k = k0; k < std::max(k1, k0 + 1); k++)
          for (int i = 0; i < rr; i++)
          {
            add(k + static_cast<double>(i) / rr, gt == kMxHold ? 1.0 / rr * 0.9 : gl / rr, (i == 0 ? 1 : 0) | (firstGate ? 2 : 0));
            firstGate = false;
          }
      }
      else if (mode == 0)                                     // multiply: rr gates spread over the gate
        for (int i = 0; i < rr; i++) { add(bs + i * bl / rr, bl / rr * 0.5, 1 | (firstGate ? 2 : 0)); firstGate = false; }
      else if (mode == 1)                                     // pulse: rr gates inside one pulse
        for (int i = 0; i < rr; i++) { add(bs + static_cast<double>(i) / rr, 0.5 / rr, (i == 0 ? 1 : 0) | (firstGate ? 2 : 0)); firstGate = false; }
      else                                                    // gated: rr gates per pulse for as long as the gate lasts
      {
        const int cnt = std::max(1, static_cast<int>(std::lround(rr * bl)));
        for (int i = 0; i < cnt; i++) { add(bs + static_cast<double>(i) / rr, 0.5 / rr, ((i % rr) == 0 ? 1 : 0) | (firstGate ? 2 : 0)); firstGate = false; }
      }
    }
    if (r.nGates > 0 && r.gates[r.nGates - 1].start + r.gates[r.nGates - 1].len >= pc - 1e-4) r.tie = true;
  }

  // the next stage of a runner (called when the current stage has used all its pulses)
  void NextStage(int t, Runner& r, bool loop)
  {
    const MxTrack& T = mPat.trk[t];
    if (loop)
    {
      if (r.loopList.empty()) return;
      const int lp = mLoopPulses.load();
      if (lp > 0 && r.totalPulses > lp) { r.seqPos = -1; r.totalPulses = 1; }
      r.seqPos = (r.seqPos + 1) % static_cast<int>(r.loopList.size());
      r.started = true; r.pulseInStage = 0;
      BuildPlan(t, r.loopList[static_cast<size_t>(r.seqPos)] & 7, r);
      return;
    }
    const int N = std::max(1, std::min(8, MI(t, kDStagesLen, T.stages.get()))), off = MI(t, kDStageOfs, T.offset.get()) & 7;
    const int lenP = std::max(0, MI(t, kDPulsesLen, T.pulses.get()));
    if (lenP > 0 && r.totalPulses > lenP) { r.seqPos = -1; r.totalPulses = 1; r.bpos = 0; r.seqKey = -1; ResetAccum(t); }
    int stage = 0;
    for (int tries = 0; tries < 24; tries++)
    {
      stage = (off + AdvanceSeq(r, std::max(0, std::min(kMxOrders - 1, MI(t, kDOrder, T.order.get()))), ((T.dir.get() != 0) != MG(t, kDDirection)) ? 1 : 0, N)) & 7;
      if ((T.st[stage].skip.get() != 0) == MG(t, kDSkipInvert)) break;
    }
    r.started = true; r.pulseInStage = 0;
    BuildPlan(t, stage, r);
  }

  void ResetAccum(int t)
  {
    TrkRt& c = mT[t];
    for (int i = 0; i < kMxStages; i++) { c.accDeg[i] = 0; c.accDir[i] = 1; }
    c.accTrk = 0; c.accTrkDir = 1;
  }

  void PulseRunner(int t, Runner& r, bool loop)
  {
    r.totalPulses++;
    if (!r.started || r.pulseInStage + 1 >= r.pulseCount) NextStage(t, r, loop);
    else r.pulseInStage++;
  }

  template <typename NoteOffF>
  void ResetTrackNow(int t, NoteOffF noteOff)
  {
    TrkRt& c = mT[t];
    if (c.sounding >= 0) noteOff(c.sounding, 0);
    const int aud = c.audNote, lastAud = c.lastAud;
    c = TrkRt();
    c.audNote = aud; c.lastAud = lastAud;
    ResetAccum(t);
  }

  template <typename NoteOffF>
  void ResetPositions(NoteOffF noteOff)
  {
    for (int t = 0; t < kMxTracks; t++) ResetTrackNow(t, noteOff);
    for (auto& l : mL) l = LaneRt();
    mGlideOutReset();
  }
  void mGlideOutReset() { mGlideOut.store(0.f); }

  // accumulator: called for every gate that starts
  void Accumulate(int t, int stage, int flags, bool force = false)
  {
    TrkRt& c = mT[t];
    const MxTrack& T = mPat.trk[t];
    const MxStage& S = T.st[stage];
    const int amt = s8(S.accAmt);
    if (amt == 0) return;
    const int trig = S.accTrig.get();
    const bool hit = force || (trig == 2) || (trig == 1 && (flags & 1)) || (trig == 0 && (flags & 2));
    if (!hit) return;
    const int lim = std::max(1, static_cast<int>(T.limPos.get())), nlim = std::max(1, static_cast<int>(T.limNeg.get()));
    const bool bipolar = T.accPolar.get() != 0, pend = T.accOrder.get() != 0;
    int& deg = (T.accMode.get() ? c.accTrk : c.accDeg[stage]);
    int& dir = (T.accMode.get() ? c.accTrkDir : c.accDir[stage]);
    if (dir == 0) dir = 1;
    deg += amt * dir;
    if (deg > lim)  { if (pend) { dir = -dir; deg = lim; } else deg = bipolar ? -nlim : 0; }
    else if (deg < -nlim) { if (pend) { dir = -dir; deg = -nlim; } else deg = bipolar ? lim : 0; }
  }

  template <typename NoteOnF, typename NoteOffF>
  void StepTrack(int t, double inc, int s, NoteOnF noteOn, NoteOffF noteOff)
  {
    TrkRt& c = mT[t];
    const MxTrack& T = mPat.trk[t];
    // ---- clock: pulses of this track (clock divider and swing)
    const double div = std::max(1, std::min(64, MI(t, kDClockDiv, T.clkDiv.get())));
    const double sw = std::max(50, std::min(78, MI(t, kDSwing, T.swing.get()))) / 100.0;
    const double lenFactor = (c.pulseIdx > 0 && ((c.pulseIdx - 1) & 1)) ? 2.0 * (1.0 - sw) : 2.0 * sw;   // pulse 1 (even) is long, pulse 2 short ...
    c.ph += inc / div / std::max(0.05, lenFactor);
    if (c.ph >= 1.0)
    {
      c.ph -= 1.0; if (c.ph >= 1.0) c.ph = 0.0;
      c.pulseIdx++;
      OnPulse(t);
    }
    Runner& R = c.loopActive ? c.loopR : c.mainR;
    bool high = false;
    int gi = -1;
    if (R.started && R.fire && !(T.mute.get() || MG(t, kDMute)))
    {
      const double pos = R.pulseInStage + c.ph;
      while (R.gi < R.nGates && pos >= R.gates[R.gi].start + R.gates[R.gi].len) R.gi++;
      if (R.gi < R.nGates && pos >= R.gates[R.gi].start)
      {
        gi = R.gi;
        if (R.decided[gi] < 0)
        {
          const MxStage& S = T.st[R.stage];
          if ((R.gates[gi].flags & 1) && S.probTarget.get() == 1 && EffProb(t, S) < 100) R.pulseOK = Rand01() < EffProb(t, S) / 100.0;
          R.decided[gi] = R.pulseOK ? 1 : 0;
        }
        high = R.decided[gi] == 1;
      }
    }
    const bool stageChanged = R.serial != c.lastSerial;
    if (high)
    {
      const bool newGate = !c.high || gi != c.lastGi || stageChanged;
      if (newGate)
      {
        const int stage = R.stage;
        const int deg = (T.accMode.get() ? c.accTrk : 0) + (T.accMode.get() ? 0 : c.accDeg[stage]);
        const int note = NoteForStage(t, stage, deg);
        const bool legato = c.high && c.prevTie && stageChanged;
        if (c.high && !legato && c.sounding >= 0) noteOff(c.sounding, s);
        const int old = c.sounding;
        noteOn(note, s);
        if (legato && old >= 0 && old != note) noteOff(old, s);
        c.sounding = note; c.lastNote = note;
        Accumulate(t, stage, R.gates[gi].flags);
        c.glide = ((T.st[stage].slide.get() || MG(t, kDSlide)) && T.slideType.get() != 2) ? MI(t, kDSlideAmt, T.slideAmt.get()) / 100.f : 0.f;
        if ((T.st[stage].slide.get() || MG(t, kDSlide)) && T.slideType.get() == 2) c.glide = 0.f;
        c.prevTie = R.tie;
      }
    }
    else if (c.high && c.sounding >= 0)
    {
      noteOff(c.sounding, s); c.sounding = -1; c.glide = 0.f;
    }
    c.high = high;
    c.lastGi = high ? gi : -1;
    c.lastSerial = R.serial;
  }

  void OnPulse(int t)
  {
    TrkRt& c = mT[t];
    // loopy: switch at a pulse
    if (c.loopArm) { StartLoop(t); c.loopArm = false; }
    if (c.loopDisarm) { c.loopActive = false; c.loopDisarm = false; }
    const int a = mLoopA[t].load(), b = mLoopB[t].load();
    if (a >= 0 && !c.loopActive) c.loopArm = true, StartLoop(t), c.loopArm = false;
    else if (a < 0 && c.loopActive) c.loopActive = false;
    else if (a >= 0 && c.loopActive && (c.loopR.loopList.empty() || c.loopR.loopList.front() != LoopFirst(t, a, b))) StartLoop(t);
    PulseRunner(t, c.mainR, false);                         // the sequence goes on silently while a loop plays
    if (c.loopActive) PulseRunner(t, c.loopR, true);
  }
  int LoopFirst(int t, int a, int b) const
  {
    const bool rev = mPat.trk[t].dir.get() != 0;
    return rev ? std::max(a, b) : std::min(a, b);
  }
  void StartLoop(int t)
  {
    TrkRt& c = mT[t];
    const int a = mLoopA[t].load(), b = mLoopB[t].load() >= 0 ? mLoopB[t].load() : mLoopA[t].load();
    if (a < 0) return;
    const int lo = std::min(a, b), hi = std::max(a, b);
    Runner& r = c.loopR;
    r.loopList.clear();
    if (mPat.trk[t].dir.get()) for (int i = hi; i >= lo; i--) r.loopList.push_back(static_cast<uint8_t>(i));
    else for (int i = lo; i <= hi; i++) r.loopList.push_back(static_cast<uint8_t>(i));
    r.started = false; r.seqPos = -1; r.pulseInStage = 0; r.pulseCount = 1; r.totalPulses = 0;
    c.loopActive = true;
    r.totalPulses = 1;
    NextStage(t, r, true);
  }

  void StepLane(int l, double inc)
  {
    LaneRt& L = mL[l];
    const MxLane& M = mPat.lane[l];
    const double div = std::max(1, std::min(64, static_cast<int>(M.clkDiv.get())));
    L.ph += inc / div;
    if (L.ph >= 1.0)
    {
      L.ph -= 1.0; if (L.ph >= 1.0) L.ph = 0.0;
      const int N = std::max(1, std::min(8, static_cast<int>(M.stages.get())));
      L.stage = (M.offset.get() + AdvanceSeq(L.r, M.order.get(), M.dir.get() ? 1 : 0, N)) & 7;
      L.r.started = true;
      L.prevVal = L.curVal;
      const int type = M.type.get();
      const float v = (type == 2) ? (M.gate[L.stage].get() ? 1.f : 0.f)
                    : (type == 1) ? (s8(M.val[L.stage]) + 127) / 254.f : s8(M.val[L.stage]) / 127.f;
      L.curVal = v;
    }
    if (!L.r.started) { L.out = 0.f; return; }
    L.out = (M.type.get() != 2 && M.ramp[L.stage].get()) ? static_cast<float>(L.prevVal + (L.curVal - L.prevVal) * L.ph) : L.curVal;
    if (M.mute.get() || mLaneMute[l].load()) L.out = 0.f;
  }

  template <typename NoteOnF, typename NoteOffF>
  void Audition(NoteOnF noteOn, NoteOffF noteOff)
  {
    for (int t = 0; t < kMxTracks; t++)
    {
      TrkRt& c = mT[t];
      const int want = mAudition[t].load();
      if (want == c.lastAud) continue;
      if (c.audNote >= 0) { noteOff(c.audNote, 0); c.audNote = -1; }
      if (want >= 0)
      {
        c.audNote = NoteForStage(t, want & 7, 0);
        noteOn(c.audNote, 0);
      }
      c.lastAud = want;
    }
  }

  void Publish()
  {
    float glide = 0.f;
    for (int t = 0; t < kMxTracks; t++)
    {
      const TrkRt& c = mT[t];
      const Runner& R = c.loopActive ? c.loopR : c.mainR;
      mPos[t].store(R.started ? static_cast<int8_t>(R.stage) : static_cast<int8_t>(-1));
      mHigh[t].store(c.high ? 1 : 0);
      mPulseLed[t].store(R.started ? static_cast<uint8_t>(R.pulseInStage + 1) : 0);
      mNote[t].store(c.sounding);
      mLoopLed[t].store(c.loopActive ? 1 : 0);
      if (c.sounding >= 0) glide = std::max(glide, c.glide);
      // CV lane of the track
      float cv = 0.f;
      if (R.started && mWasRun)
      {
        const MxStage& S = mPat.trk[t].st[R.stage];
        const float cur = s8(S.cvVal) / 127.f;
        float prev = cur;
        const int ps = (R.stage + 7) & 7;
        prev = s8(mPat.trk[t].st[ps].cvVal) / 127.f;
        const float pulseFrac = static_cast<float>(c.ph);
        if (S.cvRamp.get() == 1) cv = prev + (cur - prev) * std::min(1.f, (R.pulseInStage + pulseFrac) / std::max(1, R.pulseCount));
        else if (S.cvRamp.get() == 2) cv = prev + (cur - prev) * (R.pulseInStage == 0 ? pulseFrac : 1.f);
        else cv = cur;
        if (mPat.trk[t].cvType.get()) cv = S.cvGate.get() ? 1.f : 0.f;                 // CV type: gate toggle
        if (mPat.trk[t].mute.get()) cv = 0.f;
      }
      mCvOut[t].store(cv);
    }
    mGlideOut.store(glide);
    for (int l = 0; l < kMxLanes; l++) mLaneOut[l].store(mL[l].out);
    auto outValue = [&](int sel) -> float {
      switch (sel)
      {
        case 1: case 2: { const TrkRt& c = mT[sel - 1]; return c.lastNote < 0 ? 0.f : std::max(-1.f, std::min(1.f, (c.lastNote - 60) / 36.f)); }
        case 3: case 4: return mT[sel - 3].high ? 1.f : 0.f;
        case 5: case 6: return mCvOut[sel - 5].load();
        default: return (sel >= 7 && sel <= 14) ? mL[sel - 7].out : 0.f;
      }
    };
    mOutA.store(outValue(mPat.outA.get()));
    mOutB.store(outValue(mPat.outB.get()));
    mClkOut.store((mWasRun && mClkPhase < 0.3) ? 1 : 0);
  }

  // ---- state helpers ----------------------------------------------------------------------------
  static size_t PatternBytes(int ver = 2) { MxPattern p; size_t n = 0; p.each([&](A8&) { n++; }, ver); return n; }
  static void Put(std::vector<uint8_t>& out, const MxPattern& p) { const_cast<MxPattern&>(p).each([&](A8& a) { out.push_back(a.get()); }); }
  static void Get(const uint8_t* d, MxPattern& p, int ver = 2) { p.Init(); size_t o = 0; p.each([&](A8& a) { a.set(d[o++]); }, ver); }
};

} // namespace kr106
