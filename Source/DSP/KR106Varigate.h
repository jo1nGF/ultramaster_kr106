#pragma once

// Step sequencer modelled on the Malekko Varigate 8+.
//
//   * 8 GATE tracks x 16 steps. Per step: gate on/off, probability, repeat (ratchet), delay, pulse width.
//   * 2 CV tracks x 16 steps (tracks 9 and 10 here play notes): per step note and glide. A CV track is
//     LINKED to a gate track (it plays its note whenever that gate fires and follows its steps) or runs
//     INDEPENDENT with its own clock and a note on every step.
//   * Every track has its own clock divider / multiplier, sequence length, direction (forward, reverse,
//     pendulum, random) and mute.
//   * Notes are quantized to a scale: key + 12 allowed pitch classes per pattern.
//   * 100 presets (10 banks of 10), SONG mode (a list of presets per bank that plays one after the
//     other), randomize, freeze (turns the random behaviour into a fixed, repeating one), rotate, clear.
//
// The gate tracks also come out as modulation sources (mGateOut), so every gate row can drive the
// modulation matrix. The glide of the playing CV step comes out as mGlideOut (it opens up Porta).
//
// Everything the user edits (mPat, presets, flags) is read by the audio thread through atomics, so the
// screen can edit it at any time.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kr106 {

static constexpr int kVgGates = 8;
static constexpr int kVgTracks = 10;               // 8 gate + 2 CV
static constexpr int kVgSteps = 16;
static constexpr int kVgPresets = 100;             // 10 banks x 10
static constexpr int kVgBanks = 10;
static constexpr int kVgNumDiv = 13;

static constexpr float kVgDivFactor[kVgNumDiv] = { 1.f / 8, 1.f / 6, 1.f / 5, 1.f / 4, 1.f / 3, 1.f / 2, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f };
static constexpr const char* kVgDivNames[kVgNumDiv] = { "/8", "/6", "/5", "/4", "/3", "/2", "x1", "x2", "x3", "x4", "x5", "x6", "x8" };
static constexpr int kVgDivUnity = 6;
static constexpr const char* kVgDirNames[4] = { "Forward", "Reverse", "Pendulum", "Random" };

enum VgPage { kVgPgGate = 0, kVgPgProb, kVgPgRpt, kVgPgDly, kVgPgPw, kVgPgNote, kVgPgGlide, kVgNumPages };

// atomic byte that can be copied (so patterns can be copied around)
struct A8
{
  std::atomic<uint8_t> v { 0 };
  A8() = default;
  A8(const A8& o) { v.store(o.get(), std::memory_order_relaxed); }
  A8& operator=(const A8& o) { v.store(o.get(), std::memory_order_relaxed); return *this; }
  uint8_t get() const { return v.load(std::memory_order_relaxed); }
  void set(int x) { v.store(static_cast<uint8_t>(std::max(0, std::min(255, x))), std::memory_order_relaxed); }
};

struct VgTrack
{
  A8 gate[kVgSteps], prob[kVgSteps], rpt[kVgSteps], dly[kVgSteps], pw[kVgSteps], note[kVgSteps], glide[kVgSteps];
  A8 div, len, dir, mute, link;     // link: CV tracks only (0 = independent, 1..8 = follows that gate track)

  A8* pageArray(int page)
  {
    switch (page) {
      case kVgPgGate: return gate;  case kVgPgProb: return prob;  case kVgPgRpt: return rpt;
      case kVgPgDly:  return dly;   case kVgPgPw:   return pw;    case kVgPgNote: return note;
      default:        return glide;
    }
  }
  static int defaultValue(int page)
  {
    switch (page) { case kVgPgProb: return 127; case kVgPgPw: return 64; case kVgPgNote: return 40; default: return 0; }
  }
  void ClearSteps()
  {
    for (int p = 0; p < kVgNumPages; p++) for (int i = 0; i < kVgSteps; i++) pageArray(p)[i].set(defaultValue(p));
  }
  void Init()
  {
    ClearSteps();
    div.set(kVgDivUnity); len.set(16); dir.set(0); mute.set(0); link.set(0);
  }
};

struct VgPattern
{
  VgTrack t[kVgTracks];
  A8 key, scaleLo, scaleHi;          // key 0..11, 12-bit scale (bit i = i semitones above the key)

  uint16_t scale() const { return static_cast<uint16_t>(scaleLo.get() | ((scaleHi.get() & 0x0F) << 8)); }
  void setScale(uint16_t m) { scaleLo.set(m & 0xFF); scaleHi.set((m >> 8) & 0x0F); }
  void Init()
  {
    for (auto& tr : t) tr.Init();
    t[8].link.set(1); t[9].link.set(2);              // CV 1 follows gate 1, CV 2 follows gate 2
    key.set(0); setScale(0x0AB5);                    // C major
  }
};

// ---- note mapping: slider 0..127 -> a note of the scale, over 3 octaves from C2 --------------------
static constexpr int kVgBaseNote = 36, kVgOctaves = 3;

static inline int vgScaleNotes(int key, uint16_t mask, int* out)       // fills `out` (>= 40 entries), returns the count
{
  int n = 0;
  if ((mask & 0x0FFF) == 0) mask = 0x0FFF;
  for (int o = 0; o < kVgOctaves; o++)
    for (int pc = 0; pc < 12; pc++)
      if (mask & (1u << pc)) out[n++] = kVgBaseNote + (key % 12) + 12 * o + pc;
  out[n++] = kVgBaseNote + (key % 12) + 12 * kVgOctaves;                // the key again, one octave above
  return n;
}
static inline int vgNoteFor(const VgPattern& p, int value)
{
  int pool[48];
  const int n = vgScaleNotes(p.key.get(), p.scale(), pool);
  const int idx = std::max(0, std::min(n - 1, static_cast<int>(std::lround(static_cast<double>(value) / 127.0 * (n - 1)))));
  return std::min(127, pool[idx]);
}
static inline int vgValueForPitch(const VgPattern& p, int pitch)       // slider value that gives this pitch (or the nearest)
{
  int pool[48];
  const int n = vgScaleNotes(p.key.get(), p.scale(), pool);
  int best = 0;
  for (int i = 1; i < n; i++) if (std::abs(pool[i] - pitch) < std::abs(pool[best] - pitch)) best = i;
  return static_cast<int>(std::lround(static_cast<double>(best) * 127.0 / std::max(1, n - 1)));
}

// -------------------------------------------------------------------------------------------------
class VarigateSeq
{
public:
  VgPattern mPat;                                    // the pattern that plays and that the screen edits
  std::vector<VgPattern> mPresets;                   // 100
  A8 mSong[kVgBanks][10];                            // per bank: list of preset slots (value = slot + 1, 0 = empty)

  std::atomic<bool> mRun { false }, mSync { false }, mSongOn { false }, mFreeze { false }, mResetReq { false };
  std::atomic<float> mBpm { 120.f };
  std::atomic<int> mBank { 0 }, mSlot { 0 }, mSongPos { 0 };
  double mHostBPM = 120.0;
  bool mHostPlaying = false;

  // read by the screen / the modulation matrix
  std::atomic<int8_t> mPos[kVgTracks];               // step that plays now (-1 = none yet)
  std::atomic<uint8_t> mHigh[kVgTracks];             // gate is high
  std::atomic<float> mGateOut[kVgGates];             // 0 / 1
  std::atomic<float> mGlideOut { 0.f };              // glide of the CV step that sounds (0..1)
  std::atomic<uint8_t> mClkOut { 0 };                // CLK OUT LED: flashes on every master clock tick (16th note)
  std::atomic<uint8_t> mEndOut { 0 };                // END LED: flashes when the longest track finishes a cycle

  VarigateSeq()
  {
    mPat.Init();
    mPresets.resize(kVgPresets);
    for (auto& p : mPresets) p.Init();
    for (auto& b : mSong) for (auto& s : b) s.set(0);
    for (int i = 0; i < kVgTracks; i++) { mPos[i].store(-1); mHigh[i].store(0); }
    for (int i = 0; i < kVgGates; i++) mGateOut[i].store(0.f);
    InitDemo(mPresets[0]);
    mPat = mPresets[0];
    mSong[0][0].set(1);
  }

  void SetSampleRate(float sr) { mSampleRate = static_cast<double>(sr); }

  // a small starting pattern so that something happens when RUN is pressed
  static void InitDemo(VgPattern& p)
  {
    p.Init();
    const int pitches1[4] = { 36, 43, 40, 43 }, pitches2[4] = { 60, 64, 67, 69 };
    for (int k = 0; k < 4; k++)
    {
      p.t[0].gate[k * 4].set(127);
      p.t[8].note[k * 4].set(vgValueForPitch(p, pitches1[k]));
      p.t[1].gate[k * 4 + 2].set(127);
      p.t[9].note[k * 4 + 2].set(vgValueForPitch(p, pitches2[k]));
      p.t[1].pw[k * 4 + 2].set(40);
    }
    p.t[0].pw[0].set(96);
  }

  // ---- user actions (from the screen) --------------------------------------------------------
  void Recall(int preset)
  {
    preset = std::max(0, std::min(kVgPresets - 1, preset));
    mPat = mPresets[preset];
    mBank.store(preset / 10); mSlot.store(preset % 10);
  }
  void Save(int preset)
  {
    preset = std::max(0, std::min(kVgPresets - 1, preset));
    mPresets[preset] = mPat;
  }
  void RequestReset() { mResetReq.store(true); }

  void ClearTrack(int tr)
  {
    if (tr < 0 || tr >= kVgTracks) return;
    auto& t = mPat.t[tr];
    t.ClearSteps();
    t.mute.set(0);
  }
  void Rotate(int tr, int dir)
  {
    if (tr < 0 || tr >= kVgTracks) return;
    auto& t = mPat.t[tr];
    const int len = std::max(1, std::min(kVgSteps, static_cast<int>(t.len.get())));
    for (int p = 0; p < kVgNumPages; p++)
    {
      A8* a = t.pageArray(p);
      uint8_t tmp[kVgSteps];
      for (int i = 0; i < len; i++) tmp[i] = a[i].get();
      for (int i = 0; i < len; i++) a[i].set(tmp[((i - dir) % len + len) % len]);
    }
  }
  // Randomize one page of a track (ignored while FREEZE is on, like the hardware's frozen state)
  void Randomize(int tr, int page)
  {
    if (tr < 0 || tr >= kVgTracks || mFreeze.load()) return;
    auto& t = mPat.t[tr];
    A8* a = t.pageArray(page);
    for (int i = 0; i < kVgSteps; i++)
    {
      switch (page)
      {
        case kVgPgGate:  a[i].set(NextRand() % 100 < 45 ? 127 : 0); break;
        case kVgPgProb:  a[i].set(60 + static_cast<int>(NextRand() % 68)); break;
        case kVgPgRpt:   a[i].set(NextRand() % 100 < 65 ? 0 : static_cast<int>(NextRand() % 128)); break;
        case kVgPgDly:   a[i].set(NextRand() % 100 < 60 ? 0 : static_cast<int>(NextRand() % 70)); break;
        case kVgPgPw:    a[i].set(20 + static_cast<int>(NextRand() % 108)); break;
        case kVgPgNote:  a[i].set(static_cast<int>(NextRand() % 128)); break;
        default:         a[i].set(NextRand() % 100 < 25 ? static_cast<int>(NextRand() % 128) : 0); break;
      }
    }
  }
  void RandomizeAll(int tr) { for (int p = 0; p < kVgNumPages; p++) Randomize(tr, p); }

  // ---- state (for the plugin state) -------------------------------------------------------------
  static constexpr uint8_t kStateVersion = 1;
  void Serialize(std::vector<uint8_t>& out) const
  {
    out.push_back(kStateVersion);
    PutPattern(out, mPat);
    for (auto& p : mPresets) PutPattern(out, p);
    for (auto& b : mSong) for (auto& s : b) out.push_back(s.get());
    out.push_back(static_cast<uint8_t>(mBank.load()));
    out.push_back(static_cast<uint8_t>(mSlot.load()));
    const float bpm = mBpm.load();
    const uint8_t* bp = reinterpret_cast<const uint8_t*>(&bpm);
    for (int i = 0; i < 4; i++) out.push_back(bp[i]);
    out.push_back(mSync.load() ? 1 : 0);
    out.push_back(mSongOn.load() ? 1 : 0);
  }
  bool Deserialize(const uint8_t* d, size_t n)
  {
    const size_t pat = PatternBytes();
    const size_t need = 1 + pat * (1 + kVgPresets) + kVgBanks * 10 + 2 + 4 + 2;
    if (d == nullptr || n < need || d[0] != kStateVersion) return false;
    size_t o = 1;
    GetPattern(d + o, mPat); o += pat;
    for (auto& p : mPresets) { GetPattern(d + o, p); o += pat; }
    for (auto& b : mSong) for (auto& s : b) s.set(d[o++]);
    mBank.store(std::min<int>(kVgBanks - 1, d[o++]));
    mSlot.store(std::min<int>(9, d[o++]));
    float bpm; std::copy(d + o, d + o + 4, reinterpret_cast<uint8_t*>(&bpm)); o += 4;
    mBpm.store(std::max(20.f, std::min(400.f, bpm)));
    mSync.store(d[o++] != 0);
    mSongOn.store(d[o++] != 0);
    return true;
  }

  // ---- audio ------------------------------------------------------------------------------------
  void Reset()
  {
    for (auto& c : mCh) c = Chan();
    for (int i = 0; i < kVgTracks; i++) { mPos[i].store(-1); mHigh[i].store(0); }
    for (int i = 0; i < kVgGates; i++) mGateOut[i].store(0.f);
    mGlideOut.store(0.f);
    mClkOut.store(0); mEndOut.store(0); mClkPhase = 0.0; mEndHold = 0;
    mWasRun = false;
  }

  // the voices were silenced from outside (All Notes Off): forget the notes that were sounding
  void ForgetNotes() { for (auto& c : mCh) { c.sounding = -1; c.high = false; c.glide = 0.f; } mGlideOut.store(0.f); }

  template <typename NoteOnF, typename NoteOffF>
  void Process(int nFrames, NoteOnF noteOn, NoteOffF noteOff)
  {
    const bool sync = mSync.load();
    const bool run = sync ? mHostPlaying : mRun.load();
    if (mResetReq.exchange(false)) { ResetPositions(noteOff, 0); }
    if (!run)
    {
      if (mWasRun) { ResetPositions(noteOff, 0); mWasRun = false; Publish(); }
      return;
    }
    if (!mWasRun) { ResetPositions(noteOff, 0); mWasRun = true; }

    const double bpm = sync ? mHostBPM : static_cast<double>(mBpm.load());
    const double inc = std::max(1.0, bpm) * 4.0 / 60.0 / std::max(1.0, mSampleRate);   // 16th notes per sample
    const bool freeze = mFreeze.load();

    const int endSamples = static_cast<int>(0.08 * mSampleRate);
    for (int s = 0; s < nFrames; s++)
    {
      mClkPhase += inc;
      if (mClkPhase >= 1.0) mClkPhase -= 1.0;
      if (mEndHold > 0) mEndHold--;
      for (int tr = 0; tr < kVgTracks; tr++)
      {
        Chan& c = mCh[tr];
        if (IsLinked(tr)) continue;                                      // follows its gate track
        c.phase += inc * static_cast<double>(DivFactor(tr));
        if (c.phase >= 1.0)
        {
          c.phase -= 1.0;
          if (c.phase >= 1.0) c.phase = 0.0;
          AdvanceStep(tr, freeze, endSamples);
        }
      }
      for (int tr = 0; tr < kVgTracks; tr++)
      {
        Chan& c = mCh[tr];
        bool high = false;
        int stepIdx = c.pos;
        if (IsLinked(tr))
        {
          const Chan& g = mCh[mPat.t[tr].link.get() - 1];
          high = g.high; stepIdx = g.pos;
        }
        else if (c.started && c.fire)
        {
          const double sub = (1.0 - c.d) / c.n;
          if (c.phase >= c.d)
          {
            const int k = static_cast<int>((c.phase - c.d) / sub);
            if (k < c.n) high = (c.phase - (c.d + k * sub)) < c.w * sub;
          }
        }
        if (mPat.t[tr].mute.get()) high = false;
        if (high && !c.high)
        {
          if (tr >= kVgGates && stepIdx >= 0)
          {
            if (c.sounding >= 0) noteOff(c.sounding, s);
            c.sounding = vgNoteFor(mPat, mPat.t[tr].note[stepIdx].get());
            c.glide = mPat.t[tr].glide[stepIdx].get() / 127.f;
            noteOn(c.sounding, s);
          }
        }
        else if (!high && c.high)
        {
          if (tr >= kVgGates && c.sounding >= 0) { noteOff(c.sounding, s); c.sounding = -1; c.glide = 0.f; }
        }
        c.high = high;
      }
    }
    Publish();
  }

private:
  struct Chan
  {
    double phase = 1.0;                // position inside the step, 0..1 (1 = next sample starts the first step)
    int pos = 0, dirSign = 1, stepCount = 0;
    bool started = false, high = false, fire = false;
    double d = 0.0, w = 0.5;           // delay and pulse width (fractions of the step)
    int n = 1;                         // repeats
    int sounding = -1;
    float glide = 0.f;
  };
  Chan mCh[kVgTracks];
  double mSampleRate = 44100.0;
  bool mWasRun = false;
  double mClkPhase = 0.0;
  int mEndHold = 0;                                  // samples left of the END flash
  uint32_t mRng = 0x2545F491u, mFreezeSeed = 0x1234567u;

  uint32_t NextRand() { mRng ^= mRng << 13; mRng ^= mRng >> 17; mRng ^= mRng << 5; return mRng; }
  static uint32_t Hash(uint32_t a, uint32_t b, uint32_t c)
  {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
  }
  double Rand01(bool freeze, int tr, int step, int salt)
  {
    const uint32_t r = freeze ? Hash(mFreezeSeed, static_cast<uint32_t>(tr * 64 + step), static_cast<uint32_t>(salt)) : NextRand();
    return static_cast<double>(r >> 8) / 16777216.0;
  }

  bool IsLinked(int tr) const
  {
    if (tr < kVgGates) return false;
    const int l = mPat.t[tr].link.get();
    return l >= 1 && l <= kVgGates;
  }
  float DivFactor(int tr) const { return kVgDivFactor[std::min<int>(kVgNumDiv - 1, mPat.t[tr].div.get())]; }

  void AdvanceStep(int tr, bool freeze, int endSamples)
  {
    Chan& c = mCh[tr];
    VgTrack& t = mPat.t[tr];
    const int len = std::max(1, std::min(kVgSteps, static_cast<int>(t.len.get())));
    const int dir = std::min<int>(3, t.dir.get());
    if (!c.started)
    {
      c.started = true; c.stepCount = 0;
      c.pos = (dir == 1) ? len - 1 : 0;
      c.dirSign = (dir == 1) ? -1 : 1;
    }
    else
    {
      c.stepCount++;
      switch (dir)
      {
        case 0: c.pos = (c.pos + 1) % len; break;
        case 1: c.pos = (c.pos - 1 + len) % len; break;
        case 2:
          if (len > 1)
          {
            c.pos += c.dirSign;
            if (c.pos >= len) { c.dirSign = -1; c.pos = len - 2; }
            else if (c.pos < 0) { c.dirSign = 1; c.pos = 1; }
          }
          break;
        default:
          c.pos = static_cast<int>(Rand01(freeze, tr, c.stepCount % len, 7) * len) % len;
          break;
      }
    }
    c.pos = std::max(0, std::min(len - 1, c.pos));

    if (tr < kVgGates)
    {
      const bool on = t.gate[c.pos].get() >= 64;
      const double p = t.prob[c.pos].get() / 127.0;
      c.fire = on && (p >= 0.999 || Rand01(freeze, tr, c.pos, 1) < p);
      c.n = 1 + std::min(7, static_cast<int>(t.rpt[c.pos].get()) / 16);
      c.d = std::min(0.9, t.dly[c.pos].get() / 127.0 * 0.9);
      c.w = 0.05 + 0.95 * std::min(127, static_cast<int>(t.pw[c.pos].get())) / 127.0;
    }
    else if (!IsLinked(tr))
    {
      c.fire = true; c.n = 1; c.d = 0.0; c.w = 0.75;                      // independent CV: a note on every step
    }

    // SONG: the track with the longest cycle decides when the next preset starts
    if (tr < kVgGates && c.stepCount > 0 && (c.stepCount % len) == 0 && tr == SongLeader())
    {
      mEndHold = endSamples;                                              // END: the longest track finished a cycle
      if (mSongOn.load()) SongAdvance();
    }
  }

  int SongLeader() const
  {
    int best = 0; double bestLen = -1.0;
    for (int tr = 0; tr < kVgGates; tr++)
    {
      if (mPat.t[tr].mute.get()) continue;
      const double l = std::max(1, static_cast<int>(mPat.t[tr].len.get())) / static_cast<double>(DivFactor(tr));
      if (l > bestLen) { bestLen = l; best = tr; }
    }
    return best;
  }
  void SongAdvance()
  {
    const int bank = mBank.load();
    int pos = mSongPos.load();
    for (int tries = 0; tries < 10; tries++)
    {
      pos = (pos + 1) % 10;
      const int v = mSong[bank][pos].get();
      if (v > 0) { mSongPos.store(pos); mSlot.store(v - 1); mPat = mPresets[bank * 10 + (v - 1)]; return; }
    }
  }

  template <typename NoteOffF>
  void ResetPositions(NoteOffF noteOff, int s)
  {
    for (int tr = 0; tr < kVgTracks; tr++)
    {
      Chan& c = mCh[tr];
      if (c.sounding >= 0) { noteOff(c.sounding, s); }
      c = Chan();
    }
    mSongPos.store(0);
    Publish();
  }

  void Publish()
  {
    float glide = 0.f;
    for (int tr = 0; tr < kVgTracks; tr++)
    {
      const Chan& c = mCh[tr];
      mPos[tr].store(c.started ? static_cast<int8_t>(IsLinked(tr) ? mCh[mPat.t[tr].link.get() - 1].pos : c.pos) : static_cast<int8_t>(-1));
      mHigh[tr].store(c.high ? 1 : 0);
      if (tr < kVgGates) mGateOut[tr].store(c.high ? 1.f : 0.f);
      else if (c.sounding >= 0) glide = std::max(glide, c.glide);
    }
    mGlideOut.store(glide);
    mClkOut.store((mWasRun && mClkPhase < 0.3) ? 1 : 0);
    mEndOut.store(mEndHold > 0 ? 1 : 0);
  }

  static size_t PatternBytes() { return static_cast<size_t>(kVgTracks) * (7 * kVgSteps + 5) + 3; }
  static void PutPattern(std::vector<uint8_t>& out, const VgPattern& p)
  {
    for (const auto& t : p.t)
    {
      for (int pg = 0; pg < kVgNumPages; pg++) { const A8* a = const_cast<VgTrack&>(t).pageArray(pg); for (int i = 0; i < kVgSteps; i++) out.push_back(a[i].get()); }
      out.push_back(t.div.get()); out.push_back(t.len.get()); out.push_back(t.dir.get()); out.push_back(t.mute.get()); out.push_back(t.link.get());
    }
    out.push_back(p.key.get()); out.push_back(p.scaleLo.get()); out.push_back(p.scaleHi.get());
  }
  static void GetPattern(const uint8_t* d, VgPattern& p)
  {
    size_t o = 0;
    for (auto& t : p.t)
    {
      for (int pg = 0; pg < kVgNumPages; pg++) { A8* a = t.pageArray(pg); for (int i = 0; i < kVgSteps; i++) a[i].set(d[o++]); }
      t.div.set(std::min<int>(kVgNumDiv - 1, d[o++])); t.len.set(std::max<int>(1, std::min<int>(kVgSteps, d[o++])));
      t.dir.set(std::min<int>(3, d[o++])); t.mute.set(d[o++] ? 1 : 0); t.link.set(std::min<int>(kVgGates, d[o++]));
    }
    p.key.set(std::min<int>(11, d[o++])); p.scaleLo.set(d[o++]); p.scaleHi.set(d[o++] & 0x0F);
  }
};

} // namespace kr106
