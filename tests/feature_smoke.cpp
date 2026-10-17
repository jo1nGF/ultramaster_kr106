// Lightweight smoke test for the non-JUCE DSP changes.
// Build: c++ -std=c++17 -Wall -Wextra -Werror -ISource tests/feature_smoke.cpp -o feature_smoke
#include "DSP/KR106Arpeggiator.h"
#include "DSP/KR106ModEngine.h"

#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

int main()
{
    // Legacy source positions stay stable, while LFO 3 gets a new slot.
    assert(kr106::modSourceFromValue(0.f) == 0);
    assert(kr106::modSourceFromValue(1.f / 3.f) == 1);
    assert(kr106::modSourceFromValue(2.f / 3.f) == 2);
    assert(kr106::modSourceFromValue(1.f) == 4); // previous Envelope selection
    assert(kr106::modSourceFromValue(kr106::valueFromModSource(3)) == 3);

    // All previously existing target values retain their indices; all new targets round-trip.
    for (int target = 0; target < 37; ++target)
        assert(kr106::modTargetFromValue(static_cast<float>(target) / 36.f) == target);
    for (int target = 37; target < kr106::kNumModTargets; ++target)
        assert(kr106::modTargetFromValue(kr106::valueFromModTarget(target)) == target);
    for (int target = 0; target < kr106::kNumModTargets; ++target)
        assert(kr106::modGroupOf(target) >= 0 && kr106::modGroupOf(target) < kr106::kNumModGroups);
    assert(kr106::kNumModTargets == 69);
    assert(kr106::kNumModGroups == 12);

    kr106::Arpeggiator arp;
    arp.SetSampleRate(1000.f);
    arp.mEnabled = true;
    arp.mRate = 600.f;
    arp.NoteOn(61); // C#4
    arp.mScaleMode = 1; // Major
    arp.mScaleRoot = 0; // C
    assert(arp.QuantizeNote(61) == 60); // tie between C and D resolves downwards
    arp.mScaleRoot = 7; // G major
    assert(arp.QuantizeNote(61) == 60);

    // Probability at zero must skip every note-on; sequence timing still advances.
    int noteOns = 0;
    int noteOffs = 0;
    arp.mProbability = 0.f;
    arp.Process(100, [&](int, int) { ++noteOns; }, [&](int, int) { ++noteOffs; });
    assert(noteOns == 0);

    // Chord strum should emit the lead note first, followed by delayed chord notes.
    arp.Reset();
    arp.NoteOn(60);
    arp.NoteOn(64);
    arp.NoteOn(67);
    arp.mEnabled = true;
    arp.mChord = true;
    arp.mProbability = 1.f;
    arp.mStrum = 1.f;
    arp.mRate = 600.f;
    std::vector<std::pair<int, int>> events;
    arp.Process(100, [&](int note, int sample) { events.emplace_back(note, sample); },
                [&](int, int) { ++noteOffs; });
    assert(events.size() >= 3);
    assert(events[0].first == 60 && events[0].second == 0);
    assert(events[1].first == 64 && events[1].second > events[0].second);
    assert(events[2].first == 67 && events[2].second > events[1].second);

    std::cout << "PASS: legacy routing compatibility, 69 modulation targets, scale quantize,"
                 " probability and chord strum.\n";
    return 0;
}
