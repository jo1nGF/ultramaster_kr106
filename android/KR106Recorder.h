#pragma once

// WAV recorder for the Android host: records the final master output (what goes to
// the speakers, after the limiter) to a 24-bit stereo WAV. Writing happens on a
// background thread (juce::AudioFormatWriter::ThreadedWriter), so the audio callback
// only copies samples into a FIFO.

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

class KR106Recorder
{
public:
  KR106Recorder() : mThread("KR-106 WAV writer") { mThread.startThread(); }
  ~KR106Recorder() { stop(); mThread.stopThread(3000); }

  void setSampleRate(double sr) { mSampleRate = sr; }

  // Starts a take into 'file' (overwritten). False if the file cannot be created.
  bool start(const juce::File& file)
  {
    stop();
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    auto os = file.createOutputStream();
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), mSampleRate, 2, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();                                            // the writer owns the stream now
    mWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(w.release(), mThread, 262144);   // ~5 s of FIFO: survives a slow disk write on a phone
    mFile = file;
    mFrames.store(0);
    mPeak.store(0.f);
    mActive.store(mWriter.get(), std::memory_order_release);
    return true;
  }

  // Ends the take and closes the file (the WAV header is finalised here).
  void stop()
  {
    mActive.store(nullptr, std::memory_order_release);
    while (mBusy.load(std::memory_order_acquire) != 0) juce::Thread::sleep(1);   // audio thread left write()
    mWriter.reset();
  }

  bool isRecording() const { return mActive.load(std::memory_order_acquire) != nullptr; }
  double seconds() const { return mSampleRate > 0.0 ? (double) mFrames.load() / mSampleRate : 0.0; }
  juce::File file() const { return mFile; }
  // Peak since the last call (0..1+), for the level meter
  float takePeak() { return mPeak.exchange(0.f); }

  // Audio thread. 'data' = the output channels just produced.
  void write(const float* const* data, int numChannels, int numSamples)
  {
    mBusy.fetch_add(1, std::memory_order_acq_rel);
    if (auto* w = mActive.load(std::memory_order_acquire))
    {
      if (numChannels > 0)
      {
        const float* ch[2] = { data[0], data[numChannels > 1 ? 1 : 0] };
        w->write(ch, numSamples);
        mFrames.fetch_add(numSamples);
        float pk = 0.f;
        for (int c = 0; c < 2; c++)
          for (int i = 0; i < numSamples; i++) pk = std::max(pk, std::fabs(ch[c][i]));
        float cur = mPeak.load(std::memory_order_relaxed);
        while (pk > cur && !mPeak.compare_exchange_weak(cur, pk, std::memory_order_relaxed)) {}
      }
    }
    mBusy.fetch_sub(1, std::memory_order_acq_rel);
  }

private:
  juce::TimeSliceThread mThread;
  std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> mWriter;
  std::atomic<juce::AudioFormatWriter::ThreadedWriter*> mActive { nullptr };
  std::atomic<int> mBusy { 0 };
  std::atomic<juce::int64> mFrames { 0 };
  std::atomic<float> mPeak { 0.f };
  double mSampleRate = 48000.0;
  juce::File mFile;
};
