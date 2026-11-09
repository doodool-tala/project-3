// JNI bridge: the native B-3 engine plus a low-latency Oboe stream.
#include <jni.h>
#include <android/log.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "b3/b3.h"

#define LOG_TAG "B3Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_AUDIO(...) __android_log_print(ANDROID_LOG_INFO, "B3Audio", __VA_ARGS__)

namespace {

constexpr int kRequestedRate = 48000;
constexpr int kFramesPerCallback = 192;
constexpr int kLevelLogEvery = 32;

class Engine : public oboe::AudioStreamCallback {
 public:
  Engine() : convert_(kFramesPerCallback * 2 * 4, 0.0f) {
    organ_.prepare(kRequestedRate, 512);
  }

  ~Engine() override { stopStream(); }

  bool startStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    wantRunning_ = true;
    if (stream_) return true;
    return openLocked();
  }

  void stopStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    wantRunning_ = false;
    closeLocked();
  }

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData,
                                        int32_t numFrames) override {
    if (audioData == nullptr || numFrames <= 0) {
      return oboe::DataCallbackResult::Stop;
    }
    const int channels = stream->getChannelCount();
    if (stream->getFormat() == oboe::AudioFormat::Float) {
      organ_.processInterleaved(static_cast<float*>(audioData), numFrames);
    } else {
      int16_t* out = static_cast<int16_t*>(audioData);
      int done = 0;
      while (done < numFrames) {
        const int chunk = std::min(static_cast<int>(convert_.size()) / 4, numFrames - done);
        organ_.processInterleaved(convert_.data(), chunk);
        for (int i = 0; i < chunk; ++i) {
          for (int c = 0; c < channels; ++c) {
            const float v = std::max(-1.0f, std::min(1.0f, convert_[2 * i + (c == 0 ? 0 : 1)]));
            out[static_cast<size_t>(done + i) * channels + c] =
                static_cast<int16_t>(std::lrint(v * 32767.0f));
          }
        }
        done += chunk;
      }
    }

    // Diagnostic: prove the audio callback is running and what it produces.
    float peak = 0.0f;
    double sumSq = 0.0;
    const int64_t count = static_cast<int64_t>(numFrames) * channels;
    if (stream->getFormat() == oboe::AudioFormat::Float) {
      const float* out = static_cast<const float*>(audioData);
      for (int64_t i = 0; i < count; ++i) {
        const float v = out[i];
        peak = std::max(peak, std::fabs(v));
        sumSq += static_cast<double>(v) * v;
      }
    } else {
      const int16_t* out = static_cast<const int16_t*>(audioData);
      for (int64_t i = 0; i < count; ++i) {
        const float v = out[i] * (1.0f / 32768.0f);
        peak = std::max(peak, std::fabs(v));
        sumSq += static_cast<double>(v) * v;
      }
    }
    levelPeak_ = std::max(levelPeak_, peak);
    levelSumSq_ += sumSq;
    levelSamples_ += static_cast<uint64_t>(count);
    if (++callbackCount_ % kLevelLogEvery == 0) {
      const double rms = levelSamples_ ? std::sqrt(levelSumSq_ / levelSamples_) : 0.0;
      LOG_AUDIO("level peak=%.4f rms=%.4f", levelPeak_, rms);
      levelPeak_ = 0.0f;
      levelSumSq_ = 0.0;
      levelSamples_ = 0;
    }
    return oboe::DataCallbackResult::Continue;
  }

  void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
    LOGI("audio stream error %s, restarting", oboe::convertToText(error));
    std::lock_guard<std::mutex> lock(mutex_);
    if (!wantRunning_) return;
    closeLocked();
    openLocked();
  }

  b3::Organ& organ() { return organ_; }

 private:
  bool openLocked() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(oboe::ChannelCount::Stereo)
        ->setSampleRate(kRequestedRate)
        ->setFramesPerCallback(kFramesPerCallback)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        ->setCallback(this);

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
      // Exclusive/low-latency is not guaranteed on every device.
      builder.setSharingMode(oboe::SharingMode::Shared);
      builder.setPerformanceMode(oboe::PerformanceMode::None);
      result = builder.openStream(stream_);
    }
    if (result != oboe::Result::OK) {
      LOGE("could not open audio stream: %s", oboe::convertToText(result));
      stream_.reset();
      return false;
    }

    const int rate = stream_->getSampleRate();
    const int channels = stream_->getChannelCount();
    const int burst = stream_->getFramesPerBurst();
    LOGI("audio stream open: %d Hz, %d ch, format %d, burst %d, api %s", rate, channels,
         static_cast<int>(stream_->getFormat()), burst,
         oboe::convertToText(stream_->getAudioApi()));
    if (rate > 0) organ_.prepare(rate, 512);
    if (channels != 2) {
      LOGE("expected stereo, got %d channels", channels);
    }

    result = stream_->requestStart();
    if (result != oboe::Result::OK) {
      LOGE("could not start: %s", oboe::convertToText(result));
      closeLocked();
      return false;
    }
    LOGI("audio stream started");
    return true;
  }

  void closeLocked() {
    if (stream_) {
      stream_->stop();
      stream_->close();
      stream_.reset();
    }
  }

  std::shared_ptr<oboe::AudioStream> stream_;
  std::mutex mutex_;
  std::atomic<bool> wantRunning_{false};
  b3::Organ organ_;
  std::vector<float> convert_;
  uint32_t callbackCount_ = 0;
  float levelPeak_ = 0.0f;
  double levelSumSq_ = 0.0;
  uint64_t levelSamples_ = 0;
};

Engine* fromHandle(jlong handle) {
  return reinterpret_cast<Engine*>(static_cast<intptr_t>(handle));
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_b3organ_physical_B3Engine_nativeCreate(JNIEnv*, jobject, jint sampleRate) {
  (void)sampleRate;
  auto* engine = new Engine();
  return static_cast<jlong>(reinterpret_cast<intptr_t>(engine));
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeDestroy(JNIEnv*, jobject, jlong handle) {
  delete fromHandle(handle);
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeStart(JNIEnv*, jobject, jlong handle) {
  if (Engine* e = fromHandle(handle)) e->startStream();
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeStop(JNIEnv*, jobject, jlong handle) {
  if (Engine* e = fromHandle(handle)) e->stopStream();
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeNoteOn(JNIEnv*, jobject, jlong handle, jint note,
                                               jfloat velocity) {
  if (Engine* e = fromHandle(handle)) {
    LOGI("noteOn %d velocity %.3f", note, velocity);
    e->organ().noteOn(note, velocity);
  }
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeNoteOff(JNIEnv*, jobject, jlong handle, jint note) {
  if (Engine* e = fromHandle(handle)) {
    LOGI("noteOff %d", note);
    e->organ().noteOff(note);
  }
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeAllNotesOff(JNIEnv*, jobject, jlong handle) {
  if (Engine* e = fromHandle(handle)) {
    LOGI("allNotesOff");
    e->organ().allNotesOff();
  }
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeSetParams(JNIEnv* env, jobject, jlong handle,
                                                   jfloatArray floatArray, jintArray intArray) {
  Engine* e = fromHandle(handle);
  if (!e || !floatArray || !intArray) return;

  float f[23] = {};
  int n[6] = {};
  jsize fl = std::min<jsize>(env->GetArrayLength(floatArray), 23);
  jsize il = std::min<jsize>(env->GetArrayLength(intArray), 6);
  env->GetFloatArrayRegion(floatArray, 0, fl, f);
  env->GetIntArrayRegion(intArray, 0, il, n);

  b3::Params p;
  for (int i = 0; i < b3::kNumDrawbars; ++i) p.drawbars[i] = f[i];
  p.drive = f[9];
  p.keyClick = f[10];
  p.leakage = f[11];
  p.hum = f[12];
  p.reverb = f[13];
  p.volume = f[14];
  p.expression = f[15];
  p.micDistance = f[16];
  p.micSpread = f[17];
  p.hornDrum = f[18];
  p.tone = f[19];
  p.scannerRate = f[20];
  p.wear = f[21];
  p.cabinet = f[22];
  p.percussionOn = n[0] != 0;
  p.percussionSoft = n[1] != 0;
  p.percussionSlow = n[2] != 0;
  p.percussionThird = n[3] != 0;
  p.scanner = std::max(0, std::min(6, n[4]));
  p.leslieSpeed = std::max(0, std::min(2, n[5]));
  e->organ().setParams(p);
}

JNIEXPORT void JNICALL
Java_com_b3organ_physical_B3Engine_nativeGetTelemetry(JNIEnv* env, jobject, jlong handle,
                                                      jfloatArray out) {
  Engine* e = fromHandle(handle);
  if (!e || !out) return;
  float values[6] = {
      e->organ().hornAngleDegrees(),
      e->organ().drumAngleDegrees(),
      e->organ().scannerAngleDegrees(),
      e->organ().outputPeak(),
      static_cast<float>(e->organ().activeNotes()),
      0.0f,
  };
  const jsize len = std::min<jsize>(env->GetArrayLength(out), 6);
  env->SetFloatArrayRegion(out, 0, len, values);
}

}  // extern "C"
