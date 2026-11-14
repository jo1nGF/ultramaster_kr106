#!/bin/sh
# Builds and runs the tests from a clean temp tree (the panels include "../PluginProcessor.h", so the
# real DSP / panel headers are copied next to a small stand-in for the processor and for JUCE).
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/.." && pwd); T=${TMPDIR:-/tmp}/krtest
rm -rf "$T"; mkdir -p "$T/Source/Controls" "$T/Source/DSP" "$T/juce_gui_basics"
cp "$ROOT"/Source/DSP/*.h "$T/Source/DSP/"
cp "$ROOT"/Source/Controls/KR106VarigatePanel.h "$ROOT"/Source/Controls/KR106MetropolixPanel.h "$ROOT"/Source/Controls/KR106SeqPage.h "$ROOT"/Source/Controls/KR106PanelBackground.h "$T/Source/Controls/"
cp "$HERE/stub/PluginProcessor.h" "$T/Source/"; cp "$HERE/stub/juce_gui_basics/juce_gui_basics.h" "$T/juce_gui_basics/"
cd "$T"
echo "== panels (stand-in JUCE, sanitizers)"; g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I. -I"$HERE" "$HERE/panels_test.cpp" -o panels && ./panels
echo "== engines and DSP (sanitizers)"; g++ -std=c++17 -O1 -g -Wall -fsanitize=address,undefined -I"$ROOT" "$HERE/dsp_test.cpp" -o dsp && ./dsp
