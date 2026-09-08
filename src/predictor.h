#ifndef PREDICTOR_H
#define PREDICTOR_H

#include "mixer/sigmoid.h"
#include "mixer/mixer-input.h"
#include "mixer/mixer.h"
#include "mixer/byte-mixer.h"
#include "mixer/sse.h"
#include "models/model.h"
#include "models/byte-model.h"
#include "context-manager.h"
#include "models/direct.h"
#include "models/direct-hash.h"
#include "models/indirect.h"
#include "models/match.h"
#include "models/ppmd.h"
#include "models/bracket.h"
#include "models/fxcmv1.h"
#include "mixer/lstm.h"
#include "contexts/context-hash.h"
#include "contexts/bracket-context.h"
#include "contexts/sparse.h"
#include "contexts/indirect-hash.h"
#include "contexts/interval.h"
#include "contexts/interval-hash.h"
#include "contexts/bit-context.h"
#include "contexts/combined-context.h"

#include "ds/SmallVector.h"
#include "ds/emhash_set.hpp"

#include <vector>
#include <set>
#include <memory>
#include <optional>
#include <array>
#include <cstdint>

#ifndef CMIX_EXTRA_CONDITIONAL_EXPERT
#define CMIX_EXTRA_CONDITIONAL_EXPERT 0
#endif

#if CMIX_EXTRA_CONDITIONAL_EXPERT
// A small decoder-synchronized conditional expert.  It predicts the current
// bit from the affine relationship between the two preceding completed bytes
// (repeat, small delta, or linear continuation), the current line class, and
// the bit position.  No source pointer or side information is emitted.
class ConditionalDeltaByteExpert {
 public:
  ConditionalDeltaByteExpert(const std::vector<unsigned long long>& recent_bytes,
      const unsigned int& line_class, const unsigned int& bit_position) :
      recent_bytes_(recent_bytes), line_class_(line_class),
      bit_position_(bit_position) {
    successes_.fill(0);
    observations_.fill(0);
  }

  float Predict() const {
    const unsigned int mode = Mode();
    const unsigned int candidate = Candidate(mode);
    const unsigned int bit = (candidate >> (7U - (bit_position_ & 7U))) & 1U;
    const unsigned int index = Index(mode);
    const float agreement = static_cast<float>(successes_[index] + 1U) /
        static_cast<float>(observations_[index] + 2U);
    const float p = bit ? agreement : (1.0f - agreement);
    return p < 1.0e-4f ? 1.0e-4f : (p > 1.0f - 1.0e-4f ?
        1.0f - 1.0e-4f : p);
  }

  void Perceive(int bit) {
    const unsigned int mode = Mode();
    const unsigned int candidate = Candidate(mode);
    const unsigned int predicted =
        (candidate >> (7U - (bit_position_ & 7U))) & 1U;
    const unsigned int index = Index(mode);
    if (bit == static_cast<int>(predicted) && successes_[index] != 65535U) {
      ++successes_[index];
    }
    if (observations_[index] != 65535U) ++observations_[index];
  }

 private:
  const std::vector<unsigned long long>& recent_bytes_;
  const unsigned int& line_class_;
  const unsigned int& bit_position_;
  // 8 line classes x 16 delta modes x 8 bit positions.
  std::array<std::uint16_t, 1024> successes_{};
  std::array<std::uint16_t, 1024> observations_{};

  unsigned int Mode() const {
    const int a = static_cast<int>(recent_bytes_[0] & 255U);
    const int b = static_cast<int>(recent_bytes_[1] & 255U);
    const int delta = a - b;
    if (delta >= -7 && delta <= 7) return static_cast<unsigned int>(delta + 8);
    // Mode 15 is an explicit irregular-transition bucket.
    return 15U;
  }

  unsigned int Candidate(unsigned int mode) const {
    const unsigned int previous =
        static_cast<unsigned int>(recent_bytes_[0] & 255U);
    if (mode == 15U) return previous;
    const int delta = static_cast<int>(mode) - 8;
    int candidate = static_cast<int>(previous) + delta;
    if (candidate < 0) candidate = 0;
    if (candidate > 255) candidate = 255;
    return static_cast<unsigned int>(candidate);
  }

  unsigned int Index(unsigned int mode) const {
    return (((line_class_ & 7U) * 16U + mode) * 8U) +
        (bit_position_ & 7U);
  }
};
#endif

#ifndef CMIX_MODEL_TRACE
#define CMIX_MODEL_TRACE 0
#endif

class Predictor {
 public:
  Predictor(const std::vector<bool>& vocab);
  float Predict();
  void Perceive(int bit);
  void Pretrain(int bit);
  void FreeFxcmMemory();
  // Writes the optional causal model-family entropy diagnostic.  It is a
  // no-op in normal builds and never affects the coded probability.
  void TraceFlush();

 private:
  unsigned long long GetNumModels();
  void AddMixer(int layer, const unsigned long long& context,
      float learning_rate);
  void AddAuxiliary();
  void AddPPMD();
  void AddBracket();
  void AddWord();
  void AddDirect();
  void AddMatch();
  void AddDoubleIndirect();
  void AddMixers();

  llvm::SmallVector<Indirect<Nonstationary>, 32> indirect_ns_models_; // non-stationary
  llvm::SmallVector<Indirect<RunMap>, 1> indirect_r_models_; // run map
  llvm::SmallVector<Direct, 4> direct_models_;
  llvm::SmallVector<HashedDirect, 1> hashed_ngram_models_;
  llvm::SmallVector<Match, 10> match_models_;
  
  std::optional<Bracket> bracket_model_;
  size_t auxiliary_size_ = 2; // 0 -> fxcm, 1 -> byte_mixer
  SSE sse_;
  llvm::SmallVector<MixerInput,2> layers_;
  llvm::SmallVector<Mixer, 24> mixer_0_;
  llvm::SmallVector<Mixer, 1> mixer_1_;
  std::vector<unsigned int> auxiliary_;
  ContextManager manager_;
  Sigmoid sigmoid_;
  std::array<float, 4096> fxcm_stretched_inputs_;
  float fxcm_neutral_input_ = 0.0f;
  std::optional<PPMD::PPMD> byte_model_;
  std::optional<ByteMixer> byte_mixer_;
  std::vector<bool> vocab_;
   FXCM fxcm_model_;
#if CMIX_EXTRA_CONDITIONAL_EXPERT
  ConditionalDeltaByteExpert conditional_expert_;
#endif

#if CMIX_MODEL_TRACE
  enum TraceFamily : unsigned int {
    kTraceBracket = 0,
    kTraceFxcm,
    kTraceDirect,
    kTraceMatch,
    kTraceIndirect,
    kTracePpmd,
    kTraceByteMixer,
    kTraceFinal,
    kTraceFamilyCount
  };
  struct TraceCurrent {
    double sum_p = 0.0;
    float min_p = 1.0f;
    float max_p = 0.0f;
    unsigned int count = 0;
  };
  std::array<TraceCurrent, kTraceFamilyCount> trace_current_{};
  std::array<double, kTraceFamilyCount> trace_average_loss_{};
  std::array<double, kTraceFamilyCount> trace_best_loss_{};
  std::uint64_t trace_block_bits_ = 0;
  std::uint64_t trace_block_index_ = 0;
  std::uint64_t trace_total_bits_ = 0;
  std::array<double, kTraceFamilyCount - 1> trace_selector_weights_{};
  double trace_selector_loss_ = 0.0;
  // Non-causal diagnostic only: fixed logit interpolation between the full
  // mixer and each existing family.  These values never affect coding.
  static constexpr unsigned int kTraceAlphaCount = 33;
  std::array<std::array<double, kTraceAlphaCount>, kTraceFamilyCount - 1>
      trace_alpha_loss_{};
  std::array<double, kTraceFamilyCount - 1> trace_fisher_a_{};
  std::array<double, kTraceFamilyCount - 1> trace_fisher_b_{};
  std::array<std::array<double, kTraceFamilyCount - 1>,
      kTraceFamilyCount - 1> trace_residual_gram_{};
  static constexpr unsigned int kTraceRegionCount = 16;
  static constexpr unsigned int kTraceAutocorrLags = 64;
  std::array<std::uint64_t, kTraceRegionCount> trace_region_bits_{};
  std::array<double, kTraceRegionCount> trace_region_loss_{};
  std::array<double, kTraceAutocorrLags> trace_autocorr_products_{};
  std::array<std::uint64_t, kTraceAutocorrLags> trace_autocorr_counts_{};
  std::array<double, kTraceAutocorrLags> trace_recent_surprise_{};
  std::uint64_t trace_surprise_count_ = 0;
  double trace_surprise_sum_ = 0.0;
  unsigned int trace_recent_pos_ = 0;
  std::array<std::uint64_t, 8> trace_bitpos_bits_{};
  std::array<double, 8> trace_bitpos_loss_{};
  std::array<std::uint64_t, kTraceRegionCount * 8>
      trace_region_bitpos_bits_{};
  std::array<double, kTraceRegionCount * 8> trace_region_bitpos_loss_{};
  double trace_byte_loss_accum_ = 0.0;
  std::uint64_t trace_byte_count_ = 0;
  double trace_byte_surprise_sum_ = 0.0;
  double trace_byte_surprise_sq_sum_ = 0.0;
  std::array<double, kTraceAutocorrLags> trace_recent_byte_surprise_{};
  std::array<double, kTraceAutocorrLags> trace_byte_autocorr_products_{};
  std::array<std::uint64_t, kTraceAutocorrLags>
      trace_byte_autocorr_counts_{};
  unsigned int trace_recent_byte_pos_ = 0;
  std::array<double, kTraceAutocorrLags> trace_recent_byte_diff_{};
  std::array<double, kTraceAutocorrLags> trace_byte_diff_products_{};
  std::array<std::uint64_t, kTraceAutocorrLags>
      trace_byte_diff_counts_{};
  double trace_previous_byte_surprise_ = 0.0;
  bool trace_have_previous_byte_ = false;
  unsigned int trace_recent_byte_diff_pos_ = 0;
  // Context-conditioned shadow gate: 8 causal line classes x 128 byte-prefix
  // states, with one weight per non-final family.
  std::array<double, 1024 * (kTraceFamilyCount - 1)> trace_context_weights_{};
  double trace_context_selector_loss_ = 0.0;
  std::array<float, 2048> trace_individual_predictions_{};
  std::array<double, 2048> trace_individual_weights_{};
  unsigned int trace_individual_count_ = 0;
  double trace_individual_selector_loss_ = 0.0;
  float trace_final_prediction_ = 0.5f;
  void TraceBegin();
  void TraceAdd(TraceFamily family, float p);
  void TracePerceive(int bit);
  static double TraceLoss(float p, int bit);
#endif
};

#endif
