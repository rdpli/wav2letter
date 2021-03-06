/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "inference/examples/AudioToWords.h"

#include <fstream>
#include <functional>

#include "inference/common/IOBuffer.h"
#include "inference/examples/Util.h"

namespace w2l {
namespace streaming {

void audioStreamToWordsStream(
    std::istream& inputAudioStream,
    std::ostream& outputWordsStream,
    std::shared_ptr<Sequential> dnnModule,
    std::shared_ptr<const DecoderFactory> decoderFactory,
    const DecoderOptions& decoderOptions,
    int nTokens) {
  constexpr const int lookBack = 0;
  constexpr const size_t kWavHeaderNumBytes = 44;
  constexpr const float kMaxUint16 = static_cast<float>(0x8000);
  constexpr const int kAudioWavSamplingFrequency = 16000; // 16KHz audio.
  constexpr const int kChunkSizeMsec = 500;

  auto decoder = decoderFactory->createDecoder(decoderOptions);

  inputAudioStream.ignore(kWavHeaderNumBytes);

  const int minChunkSize = kChunkSizeMsec * kAudioWavSamplingFrequency / 1000;
  auto input = std::make_shared<streaming::ModuleProcessingState>(1);
  auto inputBuffer = input->buffer(0);
  int audioSampleCount = 0;

  // The same output object is returned by start(), run() and finish()
  auto output = dnnModule->start(input);
  auto outputBuffer = output->buffer(0);
  decoder.start();
  bool finish = false;

  outputWordsStream << "#start (msec), end(msec), transcription" << std::endl;
  while (!finish) {
    int curChunkSize = readTransformStreamIntoBuffer<int16_t, float>(
        inputAudioStream, inputBuffer, minChunkSize, [](int16_t i) -> float {
          return static_cast<float>(i) / kMaxUint16;
        });

    audioSampleCount += curChunkSize;

    if (curChunkSize >= minChunkSize) {
      dnnModule->run(input);
      float* data = outputBuffer->data<float>();
      int size = outputBuffer->size<float>();
      if (data && size > 0) {
        decoder.run(data, size);
      }
    } else {
      dnnModule->finish(input);
      float* data = outputBuffer->data<float>();
      int size = outputBuffer->size<float>();
      if (data && size > 0) {
        decoder.run(data, size);
      }
      decoder.finish();
      finish = true;
    }

    /* Print results */
    const std::vector<WordUnit> wordUnits =
        decoder.getBestHypothesisInWords(lookBack);

    outputWordsStream << audioSampleCount /
            static_cast<float>(kAudioWavSamplingFrequency / 1000.0)
                      << ","
                      << (audioSampleCount + curChunkSize) /
            static_cast<float>(kAudioWavSamplingFrequency / 1000.0)
                      << ",";
    for (const auto& wordUnit : wordUnits) {
      outputWordsStream << wordUnit.word << " ";
    }
    outputWordsStream << std::endl;

    // Consume and prune
    const int nFramesOut = outputBuffer->size<float>() / nTokens;
    outputBuffer->consume<float>(nFramesOut * nTokens);
    decoder.prune(lookBack);
  }
}

void audioFileToWordsFile(
    const std::string& inputFileName,
    const std::string& outputFileName,
    std::shared_ptr<streaming::Sequential> dnnModule,
    std::shared_ptr<const DecoderFactory> decoderFactory,
    const DecoderOptions& decoderOptions,
    int nTokens) {
  std::ifstream inputFileStream(inputFileName, std::ios::binary);
  if (!inputFileStream.is_open()) {
    throw std::runtime_error(
        "audioFileToWordsFile() failed to open input file=" + inputFileName +
        " for reading");
  }

  std::ofstream outputFileStream(outputFileName, std::ios::binary);
  if (!outputFileStream.is_open()) {
    throw std::runtime_error(
        "audioFileToWordsFile() failed to open output file=" + inputFileName +
        " inputFileName for writing");
  }

  return audioStreamToWordsStream(
      inputFileStream,
      outputFileStream,
      dnnModule,
      decoderFactory,
      decoderOptions,
      nTokens);
}

} // namespace streaming
} // namespace w2l
