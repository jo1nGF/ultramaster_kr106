// Android host for the KR-106: runs the synth + touch editor as a normal app
// (no plugin wrapper). Audio goes through JUCE's Oboe backend, MIDI comes from
// any USB / Bluetooth controller the OS exposes, and the sound is also
// playable from the on-screen "Keys" sheet.

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

class KR106Root : public juce::Component, private juce::Timer
