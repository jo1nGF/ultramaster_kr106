#pragma once

// State files: saving, validating, exporting (single file or one .zip with everything)
// and importing. A state is the raw blob from KR106AudioProcessor::getStateInformation()
// - every parameter (patch, delays, Clouds, limiter, arpeggiator extras, global settings)
// plus the Hold-latched notes - so anything the processor saves is in the export.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace kr106state
{
  inline juce::File dir(const juce::File& appData) { return appData.getChildFile("States"); }

  // Cheap sanity check before accepting a file: parameter count + enough bytes for the floats.
  inline bool isValid(const juce::MemoryBlock& mb)
  {
    if (mb.getSize() < 8) return false;
    juce::MemoryInputStream in(mb, false);
    const int n = in.readInt();
    return n >= 1 && n <= 1024 && mb.getSize() >= (size_t) (4 + 4 * n);
  }

  // A file in 'folder' for 'baseName' that does not exist yet ("name", "name (2)", ...)
  inline juce::File uniqueFile(const juce::File& folder, const juce::String& baseName)
  {
    auto clean = juce::File::createLegalFileName(baseName).trim();
    if (clean.isEmpty()) clean = "State";
    auto f = folder.getChildFile(clean + ".kr106state");
    for (int i = 2; f.exists() && i < 1000; i++)
      f = folder.getChildFile(clean + " (" + juce::String(i) + ").kr106state");
    return f;
  }

  // Imports one state blob as a new saved state. Returns the file, or {} if it is not a state.
  inline juce::File importBlob(const juce::File& folder, const juce::String& baseName, const juce::MemoryBlock& mb)
  {
    if (!isValid(mb)) return {};
    folder.createDirectory();
    auto f = uniqueFile(folder, baseName);
    return f.replaceWithData(mb.getData(), mb.getSize()) ? f : juce::File();
  }

  // One .zip with every saved state (+ the default state if there is one)
  inline bool buildExportZip(const juce::File& folder, const juce::File& defaultState, const juce::File& dest)
  {
    juce::ZipFile::Builder b;
    int count = 0;
    auto files = folder.findChildFiles(juce::File::findFiles, false, "*.kr106state");
    files.sort();
    for (auto& f : files) { b.addFile(f, 9, f.getFileName()); count++; }
    if (defaultState.existsAsFile()) { b.addFile(defaultState, 9, "default.kr106state"); count++; }
    if (count == 0) return false;
    dest.deleteFile();
    auto os = dest.createOutputStream();
    if (os == nullptr) return false;
    const bool ok = b.writeToStream(*os, nullptr);
    os->flush();
    return ok;
  }

  // Accepts a single state or a .zip of states (what buildExportZip makes). A "default.kr106state"
  // inside a zip is imported as a normal saved state named "Default (imported)".
  // Returns how many states were imported.
  inline int importData(const juce::File& folder, const juce::String& fileName, const juce::MemoryBlock& data)
  {
    const auto base = juce::File(fileName).getFileNameWithoutExtension();
    const bool isZip = fileName.endsWithIgnoreCase(".zip")
                    || (data.getSize() >= 4 && static_cast<const char*>(data.getData())[0] == 'P' && static_cast<const char*>(data.getData())[1] == 'K');
    if (isZip)
    {
      juce::MemoryInputStream in(data, false);
      juce::ZipFile zip(in);
      int n = 0;
      for (int i = 0; i < zip.getNumEntries(); i++)
      {
        auto* e = zip.getEntry(i);
        if (e == nullptr || !e->filename.endsWithIgnoreCase(".kr106state")) continue;
        std::unique_ptr<juce::InputStream> es(zip.createStreamForEntry(i));
        if (es == nullptr) continue;
        juce::MemoryBlock mb;
        es->readIntoMemoryBlock(mb);
        auto name = juce::File(e->filename).getFileNameWithoutExtension();
        if (name.equalsIgnoreCase("default")) name = "Default (imported)";
        if (!importBlob(folder, name, mb).getFullPathName().isEmpty()) n++;
      }
      return n;
    }
    return importBlob(folder, base, data).getFullPathName().isEmpty() ? 0 : 1;
  }
}
