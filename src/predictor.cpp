#include "predictor.h"
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <cstdlib>
#include <algorithm>
#include <cmath>

// Keep this at 560 for the published cmix-lex behavior.  Smaller values are a
// controlled ablation: they remove only tail FXCM inputs from the outer mixer,
// while leaving the encoder/decoder state transition exactly symmetric.
#ifndef FXCM_OUTPUT_LIMIT
#define FXCM_OUTPUT_LIMIT 560
#endif

// Keep the published cmix-lex PPMD configuration by default. Development
// builds can select a smaller state to measure the speed/ratio tradeoff
// without editing the predictor source or changing the reference build.
#ifndef PPMD_ORDER
#define PPMD_ORDER 25
#endif
#ifndef PPMD_MEMORY_MB
#define PPMD_MEMORY_MB 14000
#endif

// Macro-level predictor portfolio profiles.  The default profile (0) keeps
// the published cmix-lex construction exactly intact.  Experimental profiles
// remove complete model families at construction time, so the hot Predict(),
// Perceive(), and ByteUpdate() loops naturally execute fewer models without
// adding per-bit branches.  The transform, coder, and all remaining model
// state stay unchanged.
//
//   0 = record/default: all model families
//   1 = balanced:       omit the four double-indirect models
//   2 = fast:            profile 1 + omit the five standalone match models
//   3 = aggressive:     profile 2 + omit the word-context portfolio
//   4 = minimal:        profile 3 + omit bracket auxiliary direct/indirect
#ifndef CMIX_PROFILE
#define CMIX_PROFILE 0
#endif

// Optional causal byte-transition expert.  It predicts the next bit from the
// previously completed byte and the current byte prefix.  Disabled by
// default until an exact archive/SHA gate demonstrates a reproducible gain.
#ifndef CMIX_EXTRA_PREVBYTE_DIRECT
#define CMIX_EXTRA_PREVBYTE_DIRECT 0
#endif
#ifndef CMIX_EXTRA_BIGRAM_DIRECT
#define CMIX_EXTRA_BIGRAM_DIRECT 0
#endif
#ifndef CMIX_EXTRA_LINEBYTE_DIRECT
#define CMIX_EXTRA_LINEBYTE_DIRECT 0
#endif
#ifndef CMIX_EXTRA_HASHED_NGRAM
#define CMIX_EXTRA_HASHED_NGRAM 0
#endif
#ifndef CMIX_EXTRA_LONG_MATCH
#define CMIX_EXTRA_LONG_MATCH 0
#endif
#ifndef CMIX_EXTRA_MATCH_CONTEXT2
#define CMIX_EXTRA_MATCH_CONTEXT2 0
#endif
#ifndef CMIX_EXTRA_LINE_MATCH
#define CMIX_EXTRA_LINE_MATCH 0
#endif

// Experimental WRT-phase context.  This is deliberately opt-in: the
// existing ContextManager signal is only a causal binary indicator for the
// previous transformed byte, not a complete dictionary-token parser.
#ifndef CMIX_WRT_PHASE_MIXER
#define CMIX_WRT_PHASE_MIXER 0
#endif

static_assert(CMIX_PROFILE >= 0 && CMIX_PROFILE <= 4,
    "CMIX_PROFILE must be 0, 1, 2, 3, or 4");

Predictor::Predictor(const std::vector<bool>& vocab) : manager_(),
    sigmoid_(100001), vocab_(vocab)
#if CMIX_EXTRA_CONDITIONAL_EXPERT
    , conditional_expert_(manager_.recent_bytes_, manager_.line_class_,
        manager_.bpos)
#endif
    {
#if CMIX_MODEL_TRACE
  trace_selector_weights_.fill(1.0);
  trace_context_weights_.fill(1.0);
  trace_individual_weights_.fill(1.0);
#endif
  fxcm_neutral_input_ = sigmoid_.Logit(0.5f);
  for (int raw = -2047; raw <= 2047; ++raw) {
    float p = fxcm_model_.RawPredictionProbability(static_cast<short>(raw));
    if (p < 1.0e-4f) p = 1.0e-4f;
    else if (p > 1.0f - 1.0e-4f) p = 1.0f - 1.0e-4f;
    fxcm_stretched_inputs_[raw + 2047] = sigmoid_.Logit(p);
  }
  fxcm_stretched_inputs_[4095] = fxcm_neutral_input_;
  AddBracket();
#if CMIX_EXTRA_PREVBYTE_DIRECT
  AddDirect();
#endif
#if CMIX_EXTRA_BIGRAM_DIRECT
  // A 16-bit previous-byte context is decoder-causal and bounded. It is kept
  // opt-in because its runtime table is substantially larger than the normal
  // portfolio and must earn its memory cost with an exact archive win.
  direct_models_.emplace_back(manager_.bigram_context_, manager_.bit_context_,
      30, 0, 65536);
#endif
#if CMIX_EXTRA_LINEBYTE_DIRECT
  // Structural state and prior byte are both known before the next bit.
  direct_models_.emplace_back(manager_.line_byte_context_,
      manager_.bit_context_, 30, 0, 2048);
#endif
#if CMIX_EXTRA_HASHED_NGRAM
  hashed_ngram_models_.emplace_back(manager_.recent_bytes_,
      manager_.bit_context_, 20);
#endif
  AddPPMD();
#if CMIX_PROFILE < 3
  AddWord();
#endif
#if CMIX_PROFILE < 2
  AddMatch();
#endif
#if CMIX_PROFILE < 1
  AddDoubleIndirect();
#endif
  AddMixers();
  auxiliary_size_ = 2;
}

void Predictor::FreeFxcmMemory() {
  fxcm_model_.FreeMemory();
}

unsigned long long Predictor::GetNumModels() {
  unsigned long long num = 0;

  // models
  num += bracket_model_->NumOutputs(); // bracket
  num += std::min(fxcm_model_.NumOutputs(),
      static_cast<unsigned int>(FXCM_OUTPUT_LIMIT));
  num += direct_models_.size();
  num += hashed_ngram_models_.size();
  num += match_models_.size();
  num += indirect_ns_models_.size();
  num += indirect_r_models_.size();
  num += byte_model_->NumOutputs();
  num += byte_mixer_->NumOutputs();
#if CMIX_EXTRA_CONDITIONAL_EXPERT
  num += 1;
#endif
  return num;
}

void Predictor::AddMixer(int layer, const unsigned long long& context,
    float learning_rate) {
  if (layer == 0) {
    mixer_0_.emplace_back(
        layers_[layer].Inputs(), layers_[layer].ExtraInputs(), context,
      learning_rate, mixer_0_.size());
  } else {
    mixer_1_.emplace_back(
        layers_[layer].Inputs(), layers_[layer].ExtraInputs(), context,
      learning_rate, mixer_1_.size());
  }
}

void Predictor::AddBracket() {
  bracket_model_.emplace(manager_.bit_context_, 200, 10, 100000, vocab_);
#if CMIX_PROFILE < 4
  const Context& context = manager_.AddBracketContext(manager_.bit_context_, 256, 15);
  direct_models_.emplace_back(context.GetContext(), manager_.bit_context_, 30, 0,
      context.Size());
  indirect_ns_models_.emplace_back(manager_.nonstationary_, context.GetContext(),
      manager_.bit_context_, 300, manager_.shared_map_);
#endif
}

void Predictor::AddPPMD() {
  byte_model_.emplace(PPMD_ORDER, PPMD_MEMORY_MB,
      manager_.bit_context_, vocab_);
}

void Predictor::AddDirect() {
  // recent_bytes_[0] is updated only after a complete byte is known; paired
  // with bit_context_ this is entirely decoder-causal.  A 256x256 table is
  // bounded and independent of the corpus, so no model parameters are
  // charged to the archive.
  direct_models_.emplace_back(manager_.recent_bytes_[0], manager_.bit_context_,
      30, 0, 256);
}

void Predictor::AddWord() {
  float delta = 200;
  std::vector<std::vector<unsigned int>> model_params = {
  {0},
   {0, 1}, 
      {1}, 
      {1, 2},
      {1, 3}, 
       {2, 3},
       {3, 4},
      {1, 2, 4},
      {2, 3, 4},
       {2}
      };
  for (const auto& params : model_params) {
    const Context& context = manager_.AddSparseContext(manager_.words_, params);
    indirect_ns_models_.emplace_back(manager_.nonstationary_, context.GetContext(),
        manager_.bit_context_, delta, manager_.shared_map_);
  }

  std::vector<std::vector<unsigned int>> model_params2 = {
  {0}, 
  {1}, 
      {1, 3},
       {1, 2, 3}, 
       {7, 2}};
  for (const auto& params : model_params2) {
    const Context& context = manager_.AddSparseContext(manager_.words_, params);
    match_models_.emplace_back(manager_.history_, context.GetContext(),
        manager_.bit_context_, 200, 0.5, 2000000, &(manager_.longest_match_));
    if (params[0] == 1 && params.size() == 1) {
      indirect_r_models_.emplace_back(manager_.run_map_, context.GetContext(),
          manager_.bit_context_, delta, manager_.shared_map_);
    }
  }
#if CMIX_EXTRA_LONG_MATCH
  // A PAQ-style long-match continuation expert. It is decoder-causal because
  // it emits a non-neutral prediction only after its exact history match has
  // already continued for at least 32 bits. No source pointer is serialized.
  const Context& long_context = manager_.AddContextHashContext(
      manager_.bit_context_, 0, 8);
  match_models_.emplace_back(manager_.history_, long_context.GetContext(),
      manager_.bit_context_, 2000, 0.5,
      std::min<unsigned long long>(2000000, long_context.Size()),
      &(manager_.longest_match_), 32);
#endif
}

void Predictor::AddMatch() {
  float delta = 0.5;
  int limit = 200;
  unsigned long long max_size = 2000000;
  std::vector<std::vector<int>> model_params = {
  {0, 8}, 
  {1, 8}, 
  {7, 4},
      {11, 3}, 
      {13, 2}, 
  };

  for (const auto& params : model_params) {
    const Context& context = manager_.AddContextHashContext(manager_.bit_context_,params[0], params[1]);
    match_models_.emplace_back(manager_.history_, context.GetContext(),
        manager_.bit_context_, limit, delta, std::min(max_size, context.Size()),
        &(manager_.longest_match_));
  }
}

void Predictor::AddDoubleIndirect() {
  float delta = 400;
  indirect_ns_models_.emplace_back(manager_.nonstationary_, manager_.ind1,  manager_.bit_context_, delta, manager_.shared_map_);
  indirect_ns_models_.emplace_back(manager_.nonstationary_, manager_.ind2,  manager_.bit_context_, delta, manager_.shared_map_);
  indirect_ns_models_.emplace_back(manager_.nonstationary_, manager_.ind3,  manager_.bit_context_, delta, manager_.shared_map_);
  indirect_ns_models_.emplace_back(manager_.nonstationary_, manager_.ind5,  manager_.bit_context_, delta, manager_.shared_map_);
}

unsigned int Discretize(float p) {
  return 1 + 4094 * p;
}

#if CMIX_MODEL_TRACE
double Predictor::TraceLoss(float p, int bit) {
  if (!(p >= 0.0f)) p = 0.5f;
  if (p < 1.0e-7f) p = 1.0e-7f;
  if (p > 1.0f - 1.0e-7f) p = 1.0f - 1.0e-7f;
  const double q = bit ? static_cast<double>(p) :
      (1.0 - static_cast<double>(p));
  return -std::log2(q);
}

namespace {
float TraceLogit(float p) {
  if (p < 1.0e-6f) p = 1.0e-6f;
  if (p > 1.0f - 1.0e-6f) p = 1.0f - 1.0e-6f;
  return std::log(p / (1.0f - p));
}

float TraceSigmoid(float x) {
  if (x >= 0.0f) {
    const float z = std::exp(-x);
    return 1.0f / (1.0f + z);
  }
  const float z = std::exp(x);
  return z / (1.0f + z);
}
}

void Predictor::TraceBegin() {
  for (auto& slot : trace_current_) slot = TraceCurrent{};
  trace_individual_count_ = 0;
}

void Predictor::TraceAdd(TraceFamily family, float p) {
  if (!(p >= 0.0f)) p = 0.5f;
  if (p < 1.0e-7f) p = 1.0e-7f;
  if (p > 1.0f - 1.0e-7f) p = 1.0f - 1.0e-7f;
  auto& slot = trace_current_[family];
  slot.sum_p += p;
  if (p < slot.min_p) slot.min_p = p;
  if (p > slot.max_p) slot.max_p = p;
  ++slot.count;
  if (trace_individual_count_ < trace_individual_predictions_.size()) {
    trace_individual_predictions_[trace_individual_count_++] = p;
  }
#if CMIX_EXTRA_MATCH_CONTEXT2
  // A bounded 16-bit causal history context. Existing match experts use
  // constant, 8-bit, and much longer hashed histories; this fills the
  // measured intermediate-context gap without serializing any metadata.
  const Context& context2 = manager_.AddContextHashContext(
      manager_.bit_context_, 2, 8);
  match_models_.emplace_back(manager_.history_, context2.GetContext(),
      manager_.bit_context_, limit, delta,
      std::min<unsigned long long>(max_size, context2.Size()),
      &(manager_.longest_match_));
#endif
#if CMIX_EXTRA_LINE_MATCH
  // Select a match-history slot by the decoder-known structural line class.
  // The context has only eight states (line class 0..7), so this adds a
  // bounded causal source-history partition rather than a transmitted tag.
  const Context& line_context = manager_.AddContextHashContext(
      manager_.line_class_, 1, 3);
  match_models_.emplace_back(manager_.history_, line_context.GetContext(),
      manager_.bit_context_, limit, delta,
      std::min<unsigned long long>(max_size, line_context.Size()),
      &(manager_.longest_match_));
#endif
}

void Predictor::TracePerceive(int bit) {
  const double final_loss = TraceLoss(trace_final_prediction_, bit);
  const unsigned int region = ((manager_.line_class_ & 7U) << 1) |
      (manager_.wrt_state_ & 1U);
  const unsigned int bitpos = manager_.bpos & 7U;
  ++trace_region_bits_[region];
  trace_region_loss_[region] += final_loss;
  ++trace_bitpos_bits_[bitpos];
  trace_bitpos_loss_[bitpos] += final_loss;
  const unsigned int region_bitpos = region * 8U + bitpos;
  ++trace_region_bitpos_bits_[region_bitpos];
  trace_region_bitpos_loss_[region_bitpos] += final_loss;
  trace_surprise_count_++;
  trace_surprise_sum_ += final_loss;
  const unsigned int available = static_cast<unsigned int>(
      std::min<std::uint64_t>(trace_surprise_count_ - 1,
          kTraceAutocorrLags));
  for (unsigned int k = 1; k <= available; ++k) {
    const unsigned int index = (trace_recent_pos_ +
        kTraceAutocorrLags - k) % kTraceAutocorrLags;
    trace_autocorr_products_[k - 1] += final_loss *
        trace_recent_surprise_[index];
    ++trace_autocorr_counts_[k - 1];
  }
  trace_recent_surprise_[trace_recent_pos_] = final_loss;
  trace_recent_pos_ = (trace_recent_pos_ + 1) % kTraceAutocorrLags;
  trace_byte_loss_accum_ += final_loss;
  if (bitpos == 7U) {
    const double byte_loss = trace_byte_loss_accum_ / 8.0;
    trace_byte_loss_accum_ = 0.0;
    ++trace_byte_count_;
    trace_byte_surprise_sum_ += byte_loss;
    trace_byte_surprise_sq_sum_ += byte_loss * byte_loss;
    const unsigned int available_bytes = static_cast<unsigned int>(
        std::min<std::uint64_t>(trace_byte_count_ - 1,
            kTraceAutocorrLags));
    for (unsigned int k = 1; k <= available_bytes; ++k) {
      const unsigned int index = (trace_recent_byte_pos_ +
          kTraceAutocorrLags - k) % kTraceAutocorrLags;
      trace_byte_autocorr_products_[k - 1] += byte_loss *
          trace_recent_byte_surprise_[index];
      ++trace_byte_autocorr_counts_[k - 1];
    }
    trace_recent_byte_surprise_[trace_recent_byte_pos_] = byte_loss;
    trace_recent_byte_pos_ = (trace_recent_byte_pos_ + 1) %
        kTraceAutocorrLags;
    if (trace_have_previous_byte_) {
      const double byte_diff = byte_loss - trace_previous_byte_surprise_;
      const unsigned int available_diffs = static_cast<unsigned int>(
          std::min<std::uint64_t>(trace_byte_count_ - 2,
              kTraceAutocorrLags));
      for (unsigned int k = 1; k <= available_diffs; ++k) {
        const unsigned int index = (trace_recent_byte_diff_pos_ +
            kTraceAutocorrLags - k) % kTraceAutocorrLags;
        trace_byte_diff_products_[k - 1] += byte_diff *
            trace_recent_byte_diff_[index];
        ++trace_byte_diff_counts_[k - 1];
      }
      trace_recent_byte_diff_[trace_recent_byte_diff_pos_] = byte_diff;
      trace_recent_byte_diff_pos_ = (trace_recent_byte_diff_pos_ + 1) %
          kTraceAutocorrLags;
    }
    trace_previous_byte_surprise_ = byte_loss;
    trace_have_previous_byte_ = true;
  }
  double selector_weight_sum = 0.0;
  double selector_probability_sum = 0.0;
  std::array<float, kTraceFamilyCount - 1> family_means{};
  std::array<double, kTraceFamilyCount - 1> family_residuals{};
  for (unsigned int i = 0; i < kTraceFamilyCount; ++i) {
    auto& slot = trace_current_[i];
    if (i == kTraceFinal) {
      trace_average_loss_[i] += final_loss;
      trace_best_loss_[i] += final_loss;
      continue;
    }
    if (slot.count == 0) continue;
    const float average = static_cast<float>(slot.sum_p / slot.count);
    if (i < kTraceFamilyCount - 1) {
      family_means[i] = average;
      selector_weight_sum += trace_selector_weights_[i];
      selector_probability_sum += trace_selector_weights_[i] * average;
      const double residual = static_cast<double>(
          TraceLogit(average) - TraceLogit(trace_final_prediction_));
      family_residuals[i] = residual;
      const double gradient = static_cast<double>(trace_final_prediction_) - bit;
      const double curvature = static_cast<double>(trace_final_prediction_) *
          (1.0 - static_cast<double>(trace_final_prediction_));
      trace_fisher_a_[i] += gradient * residual;
      trace_fisher_b_[i] += curvature * residual * residual;
    }
    const float best_probability = bit ? slot.max_p : (1.0f - slot.min_p);
    trace_average_loss_[i] += TraceLoss(average, bit);
    // This is a hindsight lower bound: it chooses the family member that
    // happened to predict the observed bit best, so it is not a codec.
      trace_best_loss_[i] += TraceLoss(best_probability, 1);

    // Fixed alpha grid in logit space.  This is intentionally an optimistic
    // diagnostic: it uses a family mean that is already available to the
    // current mixer and is not a decoder state or a coding decision.
    if (i < kTraceFamilyCount - 1) {
      const float base_logit = TraceLogit(trace_final_prediction_);
      const float family_logit = TraceLogit(average);
      for (unsigned int ai = 0; ai < kTraceAlphaCount; ++ai) {
        const float alpha = (static_cast<float>(ai) - 16.0f) / 8.0f;
        const float p = TraceSigmoid(base_logit +
            alpha * (family_logit - base_logit));
        trace_alpha_loss_[i][ai] += TraceLoss(p, bit);
      }
    }
  }
  const double final_curvature = static_cast<double>(trace_final_prediction_) *
      (1.0 - static_cast<double>(trace_final_prediction_));
  for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
    if (trace_current_[i].count == 0) continue;
    for (unsigned int j = 0; j < kTraceFamilyCount - 1; ++j) {
      if (trace_current_[j].count == 0) continue;
      trace_residual_gram_[i][j] += final_curvature *
          family_residuals[i] * family_residuals[j];
    }
  }
  if (selector_weight_sum > 0.0) {
    const float selector_probability = static_cast<float>(
        selector_probability_sum / selector_weight_sum);
    trace_selector_loss_ += TraceLoss(selector_probability, bit);
    // Causal exponential-weights update. This is a small, deterministic
    // selector over family means, not a hindsight oracle and not a coder
    // change. The update uses squared error to avoid extra quantizer state.
    constexpr double eta = 0.05;
    for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
      if (trace_current_[i].count == 0) continue;
      const double error = static_cast<double>(family_means[i] - bit);
      double factor = 1.0 - eta * error * error;
      if (factor < 0.5) factor = 0.5;
      trace_selector_weights_[i] *= factor;
    }
  }
  const unsigned int context_key = ((manager_.line_class_ & 7U) << 7) |
      (manager_.bit_context_ & 127U);
  const unsigned int context_base = context_key * (kTraceFamilyCount - 1);
  double context_weight_sum = 0.0;
  double context_probability_sum = 0.0;
  for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
    if (trace_current_[i].count == 0) continue;
    const double weight = trace_context_weights_[context_base + i];
    context_weight_sum += weight;
    context_probability_sum += weight * family_means[i];
  }
  if (context_weight_sum > 0.0) {
    const float context_probability = static_cast<float>(
        context_probability_sum / context_weight_sum);
    trace_context_selector_loss_ += TraceLoss(context_probability, bit);
    constexpr double eta = 0.05;
    for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
      if (trace_current_[i].count == 0) continue;
      const double error = static_cast<double>(family_means[i] - bit);
      double factor = 1.0 - eta * error * error;
      if (factor < 0.5) factor = 0.5;
      trace_context_weights_[context_base + i] *= factor;
    }
  }
  double individual_weight_sum = 0.0;
  double individual_probability_sum = 0.0;
  for (unsigned int i = 0; i < trace_individual_count_; ++i) {
    individual_weight_sum += trace_individual_weights_[i];
    individual_probability_sum += trace_individual_weights_[i] *
        trace_individual_predictions_[i];
  }
  if (individual_weight_sum > 0.0) {
    const float individual_probability = static_cast<float>(
        individual_probability_sum / individual_weight_sum);
    trace_individual_selector_loss_ += TraceLoss(individual_probability, bit);
    constexpr double eta = 0.05;
    for (unsigned int i = 0; i < trace_individual_count_; ++i) {
      const double error = static_cast<double>(
          trace_individual_predictions_[i] - bit);
      double factor = 1.0 - eta * error * error;
      if (factor < 0.5) factor = 0.5;
      trace_individual_weights_[i] *= factor;
    }
  }
  ++trace_block_bits_;
  ++trace_total_bits_;
  if (trace_block_bits_ == 8ULL * 1000000ULL) {
    trace_block_bits_ = 0;
    ++trace_block_index_;
  }
}
#endif

void Predictor::TraceFlush() {
#if CMIX_MODEL_TRACE
  const char* path = std::getenv("CMIX_MODEL_TRACE_PATH");
  std::FILE* file = std::fopen(path ? path : "model-trace.tsv", "w");
  if (!file) return;
  static const char* names[kTraceFamilyCount] = {
    "bracket", "fxcm", "direct", "match", "indirect", "ppmd",
    "byte_mixer", "final"
  };
  std::fprintf(file, "family\tbits\tmean_loss_bits\tbest_member_loss_bits\n");
  const double denominator = trace_total_bits_ ?
      static_cast<double>(trace_total_bits_) : 1.0;
  for (unsigned int i = 0; i < kTraceFamilyCount; ++i) {
    std::fprintf(file, "%s\t%llu\t%.9f\t%.9f\n", names[i],
        static_cast<unsigned long long>(trace_total_bits_),
        trace_average_loss_[i] / denominator,
        trace_best_loss_[i] / denominator);
  }
  std::fprintf(file, "\nalpha_oracle_family\talpha\tmean_loss_bits\n");
  for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
    for (unsigned int ai = 0; ai < kTraceAlphaCount; ++ai) {
      const double alpha = (static_cast<double>(ai) - 16.0) / 8.0;
      std::fprintf(file, "%u\t%.3f\t%.9f\n", i, alpha,
          trace_alpha_loss_[i][ai] / denominator);
    }
  }
  // Formal headroom certificate.  The alpha sweep is deliberately labeled
  // non-causal: it selects the best fixed blend after observing the complete
  // sample.  It is therefore an upper bound on what reweighting these
  // existing families could recover, not a legal archive result.
  double best_oracle_loss = trace_average_loss_[kTraceFinal] / denominator;
  unsigned int best_oracle_family = kTraceFinal;
  unsigned int best_oracle_alpha = kTraceAlphaCount / 2;
  for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
    for (unsigned int ai = 0; ai < kTraceAlphaCount; ++ai) {
      const double loss = trace_alpha_loss_[i][ai] / denominator;
      if (loss < best_oracle_loss) {
        best_oracle_loss = loss;
        best_oracle_family = i;
        best_oracle_alpha = ai;
      }
    }
  }
  const double baseline_bits = trace_average_loss_[kTraceFinal];
  const double oracle_bits = best_oracle_loss * denominator;
  const double potential_bits = std::max(0.0, baseline_bits - oracle_bits);
  std::fprintf(file, "\nheadroom_certificate\tfield\tvalue\n");
  std::fprintf(file, "headroom_certificate\tstatus\tNON_CAUSAL_ORACLE\n");
  const char* scope = std::getenv("CMIX_MODEL_TRACE_SCOPE");
  const char* coverage = std::getenv("CMIX_MODEL_TRACE_COVERAGE_BYTES");
  std::fprintf(file, "headroom_certificate\toracle_scope\t%s\n",
      scope ? scope : "unspecified");
  std::fprintf(file, "headroom_certificate\toracle_hypothesis_class\t%s\n",
      "hindsight optimal static reweighting of existing model-family logits");
  std::fprintf(file, "headroom_certificate\tcoverage_bytes\t%s\n",
      coverage ? coverage : "unspecified");
  std::fprintf(file, "headroom_certificate\tbaseline_ideal_bits\t%.17g\n",
      baseline_bits);
  std::fprintf(file, "headroom_certificate\toracle_ideal_bits\t%.17g\n",
      oracle_bits);
  std::fprintf(file, "headroom_certificate\tpotential_saving_bits\t%.17g\n",
      potential_bits);
  std::fprintf(file, "headroom_certificate\tpotential_saving_bytes\t%.17g\n",
      potential_bits / 8.0);
  std::fprintf(file, "headroom_certificate\tbest_family_index\t%u\n",
      best_oracle_family);
  std::fprintf(file, "headroom_certificate\tbest_alpha_index\t%u\n",
      best_oracle_alpha);
  std::fprintf(file, "headroom_certificate\tcharged_saving_bits\t0\n");
  std::fprintf(file, "\nquadratic_local_headroom_bits\tfamily\tbits\n");
  for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
    const double a = trace_fisher_a_[i];
    const double b = trace_fisher_b_[i];
    const double gain = b > 0.0 ? (a * a) / (2.0 * b * std::log(2.0)) : 0.0;
    std::fprintf(file, "quadratic_local_headroom_bits\t%u\t%.17g\n", i, gain);
  }
  std::fprintf(file, "\nresidual_correlation\tfamily_i\tfamily_j\trho\n");
  for (unsigned int i = 0; i < kTraceFamilyCount - 1; ++i) {
    for (unsigned int j = 0; j < kTraceFamilyCount - 1; ++j) {
      const double kii = trace_residual_gram_[i][i];
      const double kjj = trace_residual_gram_[j][j];
      const double denom = std::sqrt(std::max(0.0, kii * kjj));
      const double rho = denom > 0.0 ? trace_residual_gram_[i][j] / denom : 0.0;
      std::fprintf(file, "residual_correlation\t%u\t%u\t%.17g\n", i, j,
          rho);
    }
  }
  std::fprintf(file, "causal_family_selector\t%llu\t%.9f\tNA\n",
      static_cast<unsigned long long>(trace_total_bits_),
      trace_selector_loss_ / denominator);
  std::fprintf(file, "causal_context_family_selector\t%llu\t%.9f\tNA\n",
      static_cast<unsigned long long>(trace_total_bits_),
      trace_context_selector_loss_ / denominator);
  std::fprintf(file, "causal_individual_selector\t%llu\t%.9f\tNA\n",
      static_cast<unsigned long long>(trace_total_bits_),
      trace_individual_selector_loss_ / denominator);
  std::fprintf(file, "\nregion\tbits\tmean_loss_bits\n");
  for (unsigned int region = 0; region < kTraceRegionCount; ++region) {
    const double bits = static_cast<double>(trace_region_bits_[region]);
    std::fprintf(file, "%u\t%llu\t%.9f\n", region,
        static_cast<unsigned long long>(trace_region_bits_[region]),
        bits > 0.0 ? trace_region_loss_[region] / bits : 0.0);
  }
  std::fprintf(file, "\nloss_autocorrelation\tlag\tcovariance\n");
  const double mean_loss = trace_surprise_count_ > 0 ?
      trace_surprise_sum_ / static_cast<double>(trace_surprise_count_) : 0.0;
  for (unsigned int k = 0; k < kTraceAutocorrLags; ++k) {
    const double count = static_cast<double>(trace_autocorr_counts_[k]);
    const double covariance = count > 0.0 ?
        trace_autocorr_products_[k] / count - mean_loss * mean_loss : 0.0;
    std::fprintf(file, "lag\t%u\t%.9g\n", k + 1, covariance);
  }
  std::fprintf(file, "\nbit_position\tbits\tmean_loss_bits\n");
  for (unsigned int bitpos = 0; bitpos < 8; ++bitpos) {
    const double bits = static_cast<double>(trace_bitpos_bits_[bitpos]);
    std::fprintf(file, "%u\t%llu\t%.9f\n", bitpos,
        static_cast<unsigned long long>(trace_bitpos_bits_[bitpos]),
        bits > 0.0 ? trace_bitpos_loss_[bitpos] / bits : 0.0);
  }
  std::fprintf(file, "\nbyte_loss_autocorrelation\tlag\tcovariance\n");
  const double byte_mean = trace_byte_count_ > 0 ?
      trace_byte_surprise_sum_ / static_cast<double>(trace_byte_count_) : 0.0;
  const double byte_variance = trace_byte_count_ > 0 ?
      std::max(0.0, trace_byte_surprise_sq_sum_ /
          static_cast<double>(trace_byte_count_) - byte_mean * byte_mean) :
      0.0;
  std::fprintf(file, "byte_loss_summary\tcount\tmean\tvariance\n%llu\t%.9g\t%.9g\n",
      static_cast<unsigned long long>(trace_byte_count_), byte_mean,
      byte_variance);
  for (unsigned int k = 0; k < kTraceAutocorrLags; ++k) {
    const double count = static_cast<double>(trace_byte_autocorr_counts_[k]);
    const double covariance = count > 0.0 ?
        trace_byte_autocorr_products_[k] / count - byte_mean * byte_mean : 0.0;
    std::fprintf(file, "lag\t%u\t%.9g\n", k + 1, covariance);
  }
  std::fprintf(file, "\nbyte_diff_autocorrelation\tlag\tmean_product\n");
  for (unsigned int k = 0; k < kTraceAutocorrLags; ++k) {
    const double count = static_cast<double>(trace_byte_diff_counts_[k]);
    const double mean_product = count > 0.0 ?
        trace_byte_diff_products_[k] / count : 0.0;
    std::fprintf(file, "lag\t%u\t%.9g\n", k + 1, mean_product);
  }
  std::fprintf(file, "\nregion_bit_position\tbits\tmean_loss_bits\n");
  for (unsigned int key = 0; key < kTraceRegionCount * 8U; ++key) {
    const double bits = static_cast<double>(trace_region_bitpos_bits_[key]);
    std::fprintf(file, "%u\t%llu\t%.9f\n", key,
        static_cast<unsigned long long>(trace_region_bitpos_bits_[key]),
        bits > 0.0 ? trace_region_bitpos_loss_[key] / bits : 0.0);
  }
  std::fclose(file);
#endif
}

void Predictor::AddMixers() {
  unsigned int vocab_size = 0;
  for (unsigned int i = 0; i < vocab_.size(); ++i) {
    if (vocab_[i]) ++vocab_size;
  }
#if CMIX_FAST_BYTE_MIXER
  byte_mixer_.emplace(1, manager_.bit_context_, vocab_, vocab_size, nullptr);
#else
#ifndef CMIX_LSTM_CELLS
#define CMIX_LSTM_CELLS 170
#endif
  byte_mixer_.emplace(1, manager_.bit_context_, vocab_,
      vocab_size, new Lstm(vocab_size, vocab_size, CMIX_LSTM_CELLS, 1, 128, 0.03, 10));
#endif

  for (int i = 0; i < 2; ++i) {
    layers_.emplace_back(sigmoid_,
        1.0e-4);
  }

  unsigned long long input_size = GetNumModels();
  std::cout << "num models " << input_size << "\n";
  layers_[0].SetNumModels(input_size);

  AddMixer(0, manager_.mx9, 0.005); 
  AddMixer(0, manager_.mx10, 0.0005); 
  AddMixer(0, manager_.mx11, 0.005); 
  AddMixer(0, manager_.mx12, 0.0005); 
  AddMixer(0, manager_.mx13, 0.005); 
  AddMixer(0, manager_.mxx, 0.001);
  AddMixer(0, manager_.recent_bytes_[2], 0.002);
  AddMixer(0, manager_.line_break_, 0.0007);
  AddMixer(0, manager_.longest_match_, 0.0005);
  AddMixer(0, manager_.mx19cxt, 0.002);
  AddMixer(0, manager_.auxiliary_context_, 0.0005);
  AddMixer(0, manager_.mx18, 0.001);
  AddMixer(0,manager_.mx7, 0.001);
  AddMixer(0, manager_.wordscxt, 0.005);
  AddMixer(0, manager_.b2streamcxt, 0.001);
  AddMixer(0, manager_.mx5, 0.001);
  AddMixer(0, manager_.mx6, 0.005);
  AddMixer(0, manager_.b3streamcxt, 0.001);
  AddMixer(0, manager_.mx8, 0.001);
  AddMixer(0, manager_.mx17, 0.005);
  AddMixer(0, manager_.mx16, 0.005);
  AddMixer(0, manager_.mx14, 0.005);
  AddMixer(0, manager_.mx15, 0.005);
#if CMIX_WRT_PHASE_MIXER
  // Cheap falsification gate for WRT-phase conditioning.  Keep this after
  // the published mixer portfolio so the default build is unchanged.
  AddMixer(0, manager_.wrt_phase_context_, 0.001);
#endif

  input_size = mixer_0_.size() + auxiliary_size_;
  layers_[1].SetNumModels(input_size);

  AddMixer(1,manager_.zero_context_, 0.0003);

  layers_[0].SetExtraInputSize(mixer_0_.size());

}
int lstmpr=0, lstmex=0;
float byte_mixer_output=0.0f;
float Predictor::Predict() {
#if CMIX_MODEL_TRACE
  TraceBegin();
#endif
  unsigned int input_index = 0;
  auto bracket_model_output = bracket_model_->Predict()[0];
#if CMIX_MODEL_TRACE
  TraceAdd(kTraceBracket, bracket_model_output);
#endif
  layers_[0].SetInput(input_index++, bracket_model_output);

  const unsigned int fxcm_model_outputs = std::min(fxcm_model_.NumOutputs(),
      static_cast<unsigned int>(FXCM_OUTPUT_LIMIT));
  const short* fxcm_raw_outputs = fxcm_model_.RawPredictions();
  unsigned int fxcm_active_outputs = fxcm_model_.ActivePredictions();
  if (fxcm_active_outputs > fxcm_model_outputs) fxcm_active_outputs = fxcm_model_outputs;
  // fxcmv1 emits outputs densely from zero; carrying an active count avoids
  // a hot per-bit mask scan and the old unused float prediction mirror.
  // The lookup table is already clamped to MixerInput's stretched range, so
  // these 560-ish assignments can skip duplicate bounds checks.
  for (unsigned int j = 0; j < fxcm_active_outputs; ++j) {
#if CMIX_MODEL_TRACE
    TraceAdd(kTraceFxcm, fxcm_model_.RawPredictionProbability(fxcm_raw_outputs[j]));
#endif
    layers_[0].SetStretchedInputUnchecked(
        input_index, fxcm_stretched_inputs_[fxcm_raw_outputs[j] + 2047]);
    ++input_index;
  }
  for (unsigned int j = fxcm_active_outputs; j < fxcm_model_outputs; ++j) {
#if CMIX_MODEL_TRACE
    TraceAdd(kTraceFxcm, 0.5f);
#endif
    layers_[0].SetStretchedInputUnchecked(input_index, fxcm_neutral_input_);
    ++input_index;
  }
  auto fxcm_model_index = input_index - 1;
  

  for (unsigned int i = 0; i < direct_models_.size(); ++i) {
    const std::valarray<float>& outputs = direct_models_[i].Predict();
    for (unsigned int j = 0; j < outputs.size(); ++j) {
#if CMIX_MODEL_TRACE
      TraceAdd(kTraceDirect, outputs[j]);
#endif
      layers_[0].SetInput(input_index, outputs[j]);
      ++input_index;
    }
  }

  for (unsigned int i = 0; i < match_models_.size(); ++i) {
    const std::valarray<float>& outputs = match_models_[i].Predict();
    for (unsigned int j = 0; j < outputs.size(); ++j) {
#if CMIX_MODEL_TRACE
      TraceAdd(kTraceMatch, outputs[j]);
#endif
      layers_[0].SetInput(input_index, outputs[j]);
      ++input_index;
    }
  }
 
  for (unsigned int i = 0; i < indirect_ns_models_.size(); ++i) {
    const std::valarray<float>& outputs = indirect_ns_models_[i].Predict();
    for (unsigned int j = 0; j < outputs.size(); ++j) {
#if CMIX_MODEL_TRACE
      TraceAdd(kTraceIndirect, outputs[j]);
#endif
      layers_[0].SetInput(input_index, outputs[j]);
      ++input_index;
    }
  }
 
  for (unsigned int i = 0; i < indirect_r_models_.size(); ++i) {
    const std::valarray<float>& outputs = indirect_r_models_[i].Predict();
    for (unsigned int j = 0; j < outputs.size(); ++j) {
#if CMIX_MODEL_TRACE
      TraceAdd(kTraceIndirect, outputs[j]);
#endif
      layers_[0].SetInput(input_index, outputs[j]);
      ++input_index;
    }
  }
  const float ppmd_output = byte_model_->Predict()[0];
#if CMIX_MODEL_TRACE
  TraceAdd(kTracePpmd, ppmd_output);
#endif
  layers_[0].SetInput(input_index++, ppmd_output);

#if CMIX_EXTRA_CONDITIONAL_EXPERT
  const float conditional_output = conditional_expert_.Predict();
  layers_[0].SetInput(input_index++, conditional_output);
#endif

  float byte_mixer_override = -1;

  if (byte_mixer_output == 0 || byte_mixer_output == 1) byte_mixer_override = byte_mixer_output;
#if CMIX_MODEL_TRACE
  TraceAdd(kTraceByteMixer, byte_mixer_output);
#endif
  layers_[0].SetInput(input_index++, byte_mixer_output);
  auto byte_mixer_index = input_index - 1;

  float auxiliary_average = Sigmoid::Logistic(layers_[0].Inputs()[fxcm_model_index]) + Sigmoid::Logistic(layers_[0].Inputs()[byte_mixer_index]);
  auxiliary_average /= auxiliary_size_;
  manager_.auxiliary_context_ =auxiliary_average * 15;

  for (unsigned int i = 0; i < mixer_0_.size(); ++i) {
    float p = mixer_0_[i].Mix();
    layers_[0].SetExtraInput(i, p);
    layers_[1].SetStretchedInput(i, p);
  }
  layers_[1].SetStretchedInput(mixer_0_.size(), layers_[0].Inputs()[fxcm_model_index]);
  layers_[1].SetStretchedInput(mixer_0_.size() + 1, layers_[0].Inputs()[byte_mixer_index]);

  float p = Sigmoid::Logistic(mixer_1_[0].Mix());
  p = sse_.Predict(p);
  if (byte_mixer_override >= 0) {
#if CMIX_MODEL_TRACE
    trace_final_prediction_ = byte_mixer_override;
#endif
    return byte_mixer_override;
  }
#if CMIX_EXTRA_HASHED_NGRAM
  for (unsigned int i = 0; i < hashed_ngram_models_.size(); ++i) {
    const std::valarray<float>& outputs = hashed_ngram_models_[i].Predict();
    for (unsigned int j = 0; j < outputs.size(); ++j) {
      layers_[0].SetInput(input_index, outputs[j]);
      ++input_index;
    }
  }
#endif
#if CMIX_MODEL_TRACE
  trace_final_prediction_ = p;
#endif
  return p;
}

void Predictor::Perceive(int bit) {
#if CMIX_MODEL_TRACE
  TracePerceive(bit);
#endif
  bracket_model_->Perceive(bit);

  for (unsigned int i = 0; i < direct_models_.size(); ++i) {
    direct_models_[i].Perceive(bit);
  }
#if CMIX_EXTRA_HASHED_NGRAM
  for (unsigned int i = 0; i < hashed_ngram_models_.size(); ++i) {
    hashed_ngram_models_[i].Perceive(bit);
  }
#endif
  for (unsigned int i = 0; i < match_models_.size(); ++i) {
    match_models_[i].Perceive(bit);
  }
  for (unsigned int i = 0; i < indirect_ns_models_.size(); ++i) {
    indirect_ns_models_[i].Perceive(bit);
  }
  for (unsigned int i = 0; i < indirect_r_models_.size(); ++i) {
    indirect_r_models_[i].Perceive(bit);
  }
  byte_model_->Perceive(bit);

#if CMIX_EXTRA_CONDITIONAL_EXPERT
  conditional_expert_.Perceive(bit);
#endif

  byte_mixer_->Perceive(bit);

  for (auto& mixer: mixer_0_) {
    mixer.Perceive(bit);
  }
  for (auto& mixer: mixer_1_) {
    mixer.Perceive(bit);
  }

  sse_.Perceive(bit);

  bool byte_update = false;
  if (manager_.bit_context_ >= 128) byte_update = true;

  manager_.UpdateContexts(bit);
  if (byte_update) {
    bracket_model_->ByteUpdate();

    for (unsigned int i = 0; i < direct_models_.size(); ++i) {
      direct_models_[i].ByteUpdate();
    }
#if CMIX_EXTRA_HASHED_NGRAM
    for (unsigned int i = 0; i < hashed_ngram_models_.size(); ++i) {
      hashed_ngram_models_[i].ByteUpdate();
    }
#endif
    for (unsigned int i = 0; i < match_models_.size(); ++i) {
      match_models_[i].ByteUpdate();
    }
    for (unsigned int i = 0; i < indirect_ns_models_.size(); ++i) {
      indirect_ns_models_[i].ByteUpdate();
    }

    for (unsigned int i = 0; i < indirect_r_models_.size(); ++i) {
      indirect_r_models_[i].ByteUpdate();
    }
    byte_model_->ByteUpdate();

    const std::valarray<float>& p = byte_model_->BytePredict();
    for (unsigned int j = 0; j < 256; ++j) {
      byte_mixer_->SetInput(j,p[j]);
    }

    byte_mixer_->ByteUpdate();
   
  }
  byte_mixer_output = byte_mixer_->Predict()[0];
  lstmpr=Discretize(byte_mixer_output);
  lstmex=byte_mixer_->ex;
  fxcm_model_.Perceive(bit);
  if (byte_update)manager_.bit_context_ = 1;
}

void Predictor::Pretrain(int bit) {
  bracket_model_->Predict();
  fxcm_model_.Predict();
    
  for (unsigned int i = 0; i < direct_models_.size(); ++i) {
    direct_models_[i].Predict();
  }
  for (unsigned int i = 0; i < match_models_.size(); ++i) {
    match_models_[i].Predict();
  }
  for (unsigned int i = 0; i < indirect_ns_models_.size(); ++i) {
    indirect_ns_models_[i].Predict();
  }
  for (unsigned int i = 0; i < indirect_r_models_.size(); ++i) {
    indirect_r_models_[i].Predict();
  }

  bracket_model_->Perceive(bit);
  fxcm_model_.Perceive(bit);
    
  for (unsigned int i = 0; i < direct_models_.size(); ++i) {
    direct_models_[i].Perceive(bit);
  }
  for (unsigned int i = 0; i < match_models_.size(); ++i) {
    match_models_[i].Perceive(bit);
  }
  for (unsigned int i = 0; i < indirect_ns_models_.size(); ++i) {
    indirect_ns_models_[i].Perceive(bit);
  }
  for (unsigned int i = 0; i < indirect_r_models_.size(); ++i) {
    indirect_r_models_[i].Perceive(bit);
  }

  bool byte_update = false;
  if (manager_.bit_context_ >= 128) byte_update = true;
  manager_.UpdateContexts(bit);
  if (byte_update) {
    bracket_model_->ByteUpdate();

    for (unsigned int i = 0; i < direct_models_.size(); ++i) {
      direct_models_[i].ByteUpdate();
    }
    for (unsigned int i = 0; i < match_models_.size(); ++i) {
      match_models_[i].ByteUpdate();
    }
    for (unsigned int i = 0; i < indirect_ns_models_.size(); ++i) {
      indirect_ns_models_[i].ByteUpdate();
    }
    for (unsigned int i = 0; i < indirect_r_models_.size(); ++i) {
      indirect_r_models_[i].ByteUpdate();
    }
    manager_.bit_context_ = 1;
  }
}
