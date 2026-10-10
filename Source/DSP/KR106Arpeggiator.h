#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// KR-106 arpeggiator
//
// Modes (hardware switch): Up, Up/Down, Down
// Patterns (extension, see ArpPattern): As Played, Random S&H (free or looping
// cycles), Random Walk, Converge, Diverge, Pedal, Up/Down inclusive, Down/Up.
// Range: 1 oct (held notes only), 2 oct, Full (3 oct)
// Rate: steps per minute (from slider, 60-960 BPM)
//
// The arpeggiator intercepts note-on/off and generates its own note
// events at the arp rate. Each step retriggers the gate (envelope
// re-attacks), matching the hardware behavior.

namespace kr106 {

// Note divisions for DAW sync (ordered slowest to fastest)
enum ArpDivision
{
  kDiv1 = 0,   // whole note:   0.25 steps per beat
  kDiv2,       // 1/2 note:     0.5 steps per beat
  kDiv4,       // 1/4 note:     1 step per beat
  kDiv4T,      // 1/4 triplet:  1.5 steps per beat
  kDiv8,       // 1/8 note:     2 steps per beat
  kDiv8T,      // 1/8 triplet:  3 steps per beat
  kDiv16,      // 1/16 note:    4 steps per beat
  kDiv16T,     // 1/16 triplet: 6 steps per beat
  kDiv32,      // 1/32 note:    8 steps per beat
  kNumArpDivisions
};

// Beats per arp step for each division
static constexpr double kDivBeats[kNumArpDivisions] = {
  4.0,         // 1/1
  2.0,         // 1/2
  1.0,         // 1/4
  2.0 / 3.0,   // 1/4T
  0.5,         // 1/8
  1.0 / 3.0,   // 1/8T
  0.25,        // 1/16
  1.0 / 6.0,   // 1/16T
  0.125        // 1/32
};

static constexpr const char* kDivNames[kNumArpDivisions] = {
  "4 Beats", "2 Beats", "Quarter", "Quarter Triplet", "Eighth", "Eighth Triplet", "16th Note", "16th Triplet", "32nd Note"
};

// Map slider 0-1 to division index (7 positions)
static inline int divisionFromSlider(float t)
{
  int d = static_cast<int>(std::round(t * (kNumArpDivisions - 1)));
  return std::max(0, std::min(d, kNumArpDivisions - 1));
}

// Slider value for a given division index
static inline float sliderFromDivision(int d)
{
  return static_cast<float>(d) / static_cast<float>(kNumArpDivisions - 1);
}

// ---------------------------------------------------------------------------
// Extra patterns. 0 = Classic: the hardware Mode switch (Up / Up-Down / Down)
// decides, exactly like the original. Everything else ignores that switch.
// ---------------------------------------------------------------------------
enum ArpPattern
{
  kPatClassic = 0,
  kPatUpDownIncl,   // up then down, end notes play twice
  kPatDownUp,       // starts descending
  kPatAsPlayed,     // order the keys were pressed
  kPatRandomSH,     // random note per step (sample & hold); see ArpShLoop
  kPatRandomWalk,   // random +-1 steps through the sequence
  kPatConverge,     // outside-in: low, high, 2nd low, 2nd high ...
  kPatDiverge,      // inside-out (reverse of converge)
  kPatPedal,        // lowest note alternating with the ascending sequence
  kNumArpPatterns
};

static constexpr const char* kPatternNames[kNumArpPatterns] = {
  "Classic (Mode)", "Up/Down incl.", "Down/Up", "As played", "Random S&H",
  "Random walk", "Converge", "Diverge", "Pedal"
};

// Sample & hold cycle: 0 = free-running (a new random value every step).
// Otherwise the sampled values are held in a loop of N steps that repeats;
// "evolving" loops re-sample a step now and then, so the pattern mutates.
struct ArpShLoopDef { int length; float evolve; const char* name; };
static constexpr int kNumShLoops = 13;
static constexpr ArpShLoopDef kShLoops[kNumShLoops] = {
  {  0, 0.f,   "Free" },
  {  2, 0.f,   "Loop 2" },  {  3, 0.f, "Loop 3" },  { 4, 0.f, "Loop 4" },
  {  5, 0.f,   "Loop 5" },  {  6, 0.f, "Loop 6" },  { 7, 0.f, "Loop 7" },
  {  8, 0.f,   "Loop 8" },  { 12, 0.f, "Loop 12" }, { 16, 0.f, "Loop 16" },
  {  4, 0.25f, "Evolve 4" }, { 8, 0.25f, "Evolve 8" }, { 16, 0.25f, "Evolve 16" }
};

// Parameter value (0..1) <-> index helpers for the two discrete params
static inline int patternFromValue(float v)
{ return std::max(0, std::min(kNumArpPatterns - 1, static_cast<int>(std::round(v * (kNumArpPatterns - 1))))); }
static inline float valueFromPattern(int i) { return static_cast<float>(i) / (kNumArpPatterns - 1); }
static inline int shLoopFromValue(float v)
{ return std::max(0, std::min(kNumShLoops - 1, static_cast<int>(std::round(v * (kNumShLoops - 1))))); }
static inline float valueFromShLoop(int i) { return static_cast<float>(i) / (kNumShLoops - 1); }

struct Arpeggiator
{
  bool mEnabled = false;
  int mMode = 0;       // 0=Up, 1=Up/Down, 2=Down
  int mRange = 0;      // 0=1oct, 1=2oct, 2=3oct
  float mRate = 120.f; // steps per minute (mapped from slider)
  float mSampleRate = 44100.f;

  // DAW sync state (set by processor each block)
  bool mSyncToHost = false;
  bool mHostPlaying = false;
  double mHostBPM = 120.0;
  double mHostBeatPos = 0.0;   // beat position at start of current block
  int mDivision = kDiv16;      // current note division (when synced)
  int64_t mLastSyncStep = -1;  // last beat-grid step index (for edge detection)

  /*
  * arp_rate_hz — Juno-106 Arpeggio Rate oscillator frequency
  *
  * The VR1 section is a Schmitt-trigger relaxation oscillator: capacitor C4
  * charges and discharges through the total timing resistance Rt = R21 + VR1,
  * switching between two thresholds set by the R16/R17 positive-feedback
  * divider on the op-amp's non-inverting input.
  *
  * The threshold fraction is α = R16/(R16+R17) = 47K/147K ≈ 0.3197, so on
  * a ±15V supply the cap swings between −4.796V and +4.796V each half-cycle.
  *
  * Solving the RC charging equation for the half-period:
  *   t_half = Rt · C4 · ln((1+α)/(1−α))
  *          = Rt · C4 · ln(1.3197/0.6803)
  *          ≈ Rt · C4 · 0.6633
  *
  * Full period T = 2 · t_half, so:
  *   f = 1 / (2 · Rt · C4 · 0.6633)
  *     = 1 / (2 · (R21 + pos·VR1_MAX) · C4 · 0.6633)
  *
  * pos=0 → Rt=33K  → f ≈ 48.5 Hz  (fastest arp rate)
  * pos=1 → Rt=1.033M → f ≈  1.55 Hz  (slowest arp rate)
  *
  * Note: response is hyperbolic (f ∝ 1/pos), not linear — the pot
  * is linear taper but the perceived curve is naturally logarithmic.
  */
  static float arpRate(float t) {
    // Our original 2001 linear fader code return 15.f + t * 1785.f;

    // Schmitt-trigger relaxation oscillator: f in Hz, convert to BPM
    // Invert slider: 0=slowest (max resistance), 1=fastest (min resistance)
    float pos = 1.f - t;
    float hz = 1.0f / (2.0f * (33000.0f + pos * 1000000.0f) * 0.47e-6f * 0.6633f);
    return hz * 60.f;
  }

  std::vector<int> mHeldNotes; // sorted ascending
  std::vector<int> mPlayOrder; // same notes, in the order they were pressed

  // Extension state
  int mPattern = 0;                 // ArpPattern
  int mShLoop = 0;                  // index into kShLoops
  uint32_t mRng = 0x2545F491u;
  float mShBuf[16] = {};            // held random values of the current cycle
  bool mShValid[16] = {};
  int mShPos = 0;
  int mWalkPos = 0;
  int mCounter = 0;                 // step counter for the patterned modes

  float Rand()                      // 0..1
  {
    mRng = mRng * 1664525u + 1013904223u;
    return static_cast<float>(mRng >> 8) * (1.f / 16777216.f);
  }
  void SetShLoop(int idx)
  {
    idx = std::max(0, std::min(kNumShLoops - 1, idx));
    if (idx != mShLoop) { mShLoop = idx; ResetSh(); }
  }
  void ResetSh() { mShPos = 0; for (bool& v : mShValid) v = false; }
  // Next sample & hold value (0..1)
  float NextSh()
  {
    const auto& d = kShLoops[mShLoop];
    if (d.length == 0) return Rand();            // free-running S&H
    int i = mShPos % d.length;
    if (!mShValid[i]) { mShBuf[i] = Rand(); mShValid[i] = true; }
    else if (d.evolve > 0.f && Rand() < d.evolve) mShBuf[i] = Rand();   // mutate this step
    mShPos = (i + 1) % d.length;
    return mShBuf[i];
  }

  Arpeggiator() { mHeldNotes.reserve(128); }
  int mStepIndex = 0;
  int mDirection = 1;  // 1=ascending, -1=descending (for Up/Down)
  float mPhase = 0.f;
  int mLastNote = -1;  // currently sounding arp note
  std::atomic<uint32_t> mTickCount{0}; // incremented each arp step (UI can poll)

  void SetSampleRate(float sr) { mSampleRate = sr; }

  void NoteOn(int note)
  {
    if (note < 0 || note > 127) return;
    auto it = std::lower_bound(mHeldNotes.begin(), mHeldNotes.end(), note);
    if (it != mHeldNotes.end() && *it == note) return; // already held
    mHeldNotes.insert(it, note);
    mPlayOrder.push_back(note);

    // First note in: trigger immediately on next process call
    if (mHeldNotes.size() == 1)
    {
      mPhase = 1.f;
      mStepIndex = 0;
      mDirection = 1;
      mCounter = 0;
      mWalkPos = 0;
      mShPos = 0;          // loops restart together with the first note
    }
  }

  void NoteOff(int note)
  {
    auto it = std::find(mHeldNotes.begin(), mHeldNotes.end(), note);
    if (it != mHeldNotes.end())
      mHeldNotes.erase(it);
    {
      auto po = std::find(mPlayOrder.begin(), mPlayOrder.end(), note);
      if (po != mPlayOrder.end()) mPlayOrder.erase(po);
    }

    if (mHeldNotes.empty())
    {
      mStepIndex = 0;
      mDirection = 1;
      mPhase = 0.f;
    }
  }

  void Reset()
  {
    mHeldNotes.clear();
    mPlayOrder.clear();
    mCounter = 0; mWalkPos = 0; mShPos = 0;
    mStepIndex = 0;
    mDirection = 1;
    mPhase = 0.f;
    mLastNote = -1;
    mLastSyncStep = -1;
  }

  // Physical keyboard top note (Juno-106: 61 keys, C1–C6, MIDI 36–96).
  // When mLimitToKeyboard is true, notes transposed above this wrap down
  // by octaves until they fit — matching hardware behavior where the arp
  // cannot produce notes above the physical keyboard.
  static constexpr int kMaxArpNote = 96;
  bool mLimitToKeyboard = true;

  // Full sequence length — all slots always present (wrapped notes still play)
  int SeqLen() const
  {
    if (mLimitToKeyboard)
      return static_cast<int>(mHeldNotes.size()) * (mRange + 1);

    int count = 0;
    int octaves = mRange + 1;
    for (int oct = 0; oct < octaves; oct++)
      for (int n : mHeldNotes)
        if (n + oct * 12 <= 127) count++;
    return count;
  }

  // Note at ascending index in the full sequence (base = held notes sorted,
  // or in play order for "As played")
  int SeqNote(int idx) const { return SeqNoteFrom(mHeldNotes, idx); }
  int SeqNoteFrom(const std::vector<int>& base, int idx) const
  {
    int i = 0;
    int octaves = mRange + 1;
    for (int oct = 0; oct < octaves; oct++)
      for (int n : base)
      {
        int note = n + oct * 12;
        if (mLimitToKeyboard)
        {
          while (note > kMaxArpNote) note -= 12;
        }
        else
        {
          if (note > 127) continue;
        }
        if (i == idx) return note;
        i++;
      }
    return -1;
  }

  // Advance step and return the next note to play
  int NextNote()
  {
    int len = SeqLen();
    if (len == 0) return -1;

    // Clamp after sequence changes
    if (mStepIndex >= len) mStepIndex = 0;
    if (mStepIndex < 0) mStepIndex = len - 1;

    if (mPattern != kPatClassic)
      return NextPatternNote(len);

    int note;
    switch (mMode)
    {
      case 0: // Up
        note = SeqNote(mStepIndex);
        mStepIndex = (mStepIndex + 1) % len;
        break;

      case 1: // Up/Down — peak and trough notes play once
        note = SeqNote(mStepIndex);
        if (len > 1)
        {
          mStepIndex += mDirection;
          if (mStepIndex >= len) { mStepIndex = len - 2; mDirection = -1; }
          else if (mStepIndex < 0) { mStepIndex = 1; mDirection = 1; }
        }
        break;

      case 2: // Down
        note = SeqNote(len - 1 - mStepIndex);
        mStepIndex = (mStepIndex + 1) % len;
        break;

      default:
        note = SeqNote(0);
    }
    return note;
  }

  // Extra patterns (everything except Classic)
  int NextPatternNote(int len)
  {
    if (len <= 0) return -1;
    int c = mCounter++;
    auto at = [&](int idx) { return SeqNote(std::max(0, std::min(len - 1, idx))); };
    switch (mPattern)
    {
      case kPatUpDownIncl:                   // 0..len-1, len-1..0 (period 2*len)
      {
        int k = c % (2 * len);
        return at(k < len ? k : 2 * len - 1 - k);
      }
      case kPatDownUp:                       // len-1..0..len-1 without repeating the ends
      {
        if (len == 1) return at(0);
        int per = 2 * len - 2, k = c % per;
        return at(k < len ? len - 1 - k : k - len + 1);
      }
      case kPatAsPlayed:
        return SeqNoteFrom(mPlayOrder, c % len);
      case kPatRandomSH:
        return at(static_cast<int>(NextSh() * static_cast<float>(len)));
      case kPatRandomWalk:
      {
        float r = Rand();
        int step = r < 0.4f ? 1 : r < 0.8f ? -1 : 0;   // sometimes repeats the note
        mWalkPos += step;
        if (mWalkPos >= len) mWalkPos = std::max(0, len - 2);   // bounce at the ends
        if (mWalkPos < 0) mWalkPos = std::min(1, len - 1);
        return at(mWalkPos);
      }
      case kPatConverge:                     // 0, len-1, 1, len-2, ...
      {
        int k = c % len;
        return at((k & 1) ? len - 1 - k / 2 : k / 2);
      }
      case kPatDiverge:                      // reverse of converge: inside-out
      {
        int k = (len - 1) - (c % len);
        return at((k & 1) ? len - 1 - k / 2 : k / 2);
      }
      case kPatPedal:                        // lowest note between every step
      {
        int k = c % (2 * len);
        return (k & 1) ? at(k / 2) : at(0);
      }
      default:
        return at(0);
    }
  }

  // Process one block. Calls noteOn(noteNum, sampleOffset) and
  // noteOff(noteNum, sampleOffset) for each arp step.
  template <typename NoteOnF, typename NoteOffF>
  void Process(int nFrames, NoteOnF noteOn, NoteOffF noteOff)
  {
    if (!mEnabled || mHeldNotes.empty())
    {
      // Release lingering arp note when disabled or all keys released
      if (mLastNote >= 0)
      {
        noteOff(mLastNote, 0);
        mLastNote = -1;
      }
      mLastSyncStep = -1;
      return;
    }

    if (mSyncToHost)
    {
      int div = std::max(0, std::min(mDivision, static_cast<int>(kNumArpDivisions) - 1));
      double divBeats = kDivBeats[div];

      if (mHostPlaying)
      {
        // Transport running: lock steps to beat grid
        double beatsPerSample = mHostBPM / (60.0 * static_cast<double>(mSampleRate));

        for (int s = 0; s < nFrames; s++)
        {
          double beatPos = mHostBeatPos + s * beatsPerSample;
          int64_t stepNow = static_cast<int64_t>(std::floor(beatPos / divBeats));

          if (stepNow != mLastSyncStep)
          {
            mLastSyncStep = stepNow;
            mTickCount.fetch_add(1, std::memory_order_relaxed);

            if (mLastNote >= 0)
              noteOff(mLastNote, s);

            int note = NextNote();
            if (note >= 0)
            {
              noteOn(note, s);
              mLastNote = note;
            }
          }
        }
        return;
      }

      // Transport stopped: free-run at tempo-matched rate
      float syncRate = static_cast<float>(mHostBPM / divBeats);
      float inc = syncRate / (60.f * mSampleRate);

      for (int s = 0; s < nFrames; s++)
      {
        mPhase += inc;
        if (mPhase >= 1.f)
        {
          mPhase -= 1.f;
          mTickCount.fetch_add(1, std::memory_order_relaxed);

          if (mLastNote >= 0)
            noteOff(mLastNote, s);

          int note = NextNote();
          if (note >= 0)
          {
            noteOn(note, s);
            mLastNote = note;
          }
        }
      }
      return;
    }

    // Free-running mode: phase accumulator at mRate steps/minute
    float inc = mRate / (60.f * mSampleRate);

    for (int s = 0; s < nFrames; s++)
    {
      mPhase += inc;
      if (mPhase >= 1.f)
      {
        mPhase -= 1.f;
        mTickCount.fetch_add(1, std::memory_order_relaxed);

        // Release previous arp note
        if (mLastNote >= 0)
          noteOff(mLastNote, s);

        // Trigger next arp note
        int note = NextNote();
        if (note >= 0)
        {
          noteOn(note, s);
          mLastNote = note;
        }
      }
    }
  }
};

} // namespace kr106
