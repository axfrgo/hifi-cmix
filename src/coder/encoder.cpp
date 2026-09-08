#include "encoder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>

#ifndef CMIX_ENTROPY_TRACE
#define CMIX_ENTROPY_TRACE 0
#endif

#if CMIX_ENTROPY_TRACE || CMIX_SHADOW_TRACE
namespace {
const std::array<double, 65536>& TraceLossTable() {
  static const std::array<double, 65536> table = [] {
    std::array<double, 65536> values{};
    for (unsigned int i = 1; i < 65535; ++i) {
      values[i] = -std::log2(static_cast<double>(i) / 65535.0);
    }
    return values;
  }();
  return table;
}
}  // namespace
#endif

Encoder::Encoder(std::ofstream* os, Predictor* p) : os_(os), x1_(0),
    x2_(0xffffffff), p_(p)
#if CMIX_ENTROPY_TRACE
    , trace_(std::fopen("entropy-trace.tsv", "w")), trace_bits_(0),
      trace_block_bits_(0), trace_block_loss_(0.0), trace_block_index_(0)
#endif
#if CMIX_SHADOW_TRACE
    , shadow_trace_(std::fopen(std::getenv("CMIX_SHADOW_TRACE_PATH") ?
          std::getenv("CMIX_SHADOW_TRACE_PATH") : "shadow-trace.tsv", "w"))
#endif
{}

void Encoder::WriteByte(unsigned int byte) {
  out_.push_back(byte);
}

unsigned int Encoder::Discretize(float p) {
  return 1 + 65534 * p;
}

void Encoder::Encode(int bit) {
  const float prediction = p_->Predict();
  const unsigned int p = Discretize(prediction);
#if CMIX_SHADOW_TRACE
  // Use the exact same quantized event cost as the range coder. The candidate
  // sees only state available before this bit, so this is decoder-causal.
  const std::uint32_t shadow_index =
      (shadow_prev_byte_ << 8) | shadow_prefix_;
  const std::uint32_t total = shadow_totals_[shadow_index];
  const std::uint32_t ones = shadow_ones_[shadow_index];
  const double candidate_p = static_cast<double>(ones + 1) /
      static_cast<double>(total + 2);
  const unsigned int candidate_q = Discretize(static_cast<float>(candidate_p));
  shadow_mixed_loss_ += TraceLossTable()[bit ? p : (65535U - p)];
  shadow_candidate_loss_ += TraceLossTable()[bit ? candidate_q :
      (65535U - candidate_q)];
#endif
#if CMIX_ENTROPY_TRACE
  // Measure the probability actually presented to the range coder.  This is
  // a diagnostic only; it does not alter the coding path or predictor state.
  const unsigned int event = bit ? p : (65535U - p);
  trace_block_loss_ += TraceLossTable()[event];
  ++trace_bits_;
  ++trace_block_bits_;
#endif
  const unsigned int xmid = x1_ + ((x2_ - x1_) >> 16) * p +
      (((x2_ - x1_) & 0xffff) * p >> 16);
  if (bit) {
    x2_ = xmid;
  } else {
    x1_ = xmid + 1;
  }
  p_->Perceive(bit);
#if CMIX_SHADOW_TRACE
  shadow_totals_[shadow_index] = total + 1;
  shadow_ones_[shadow_index] = ones + static_cast<std::uint32_t>(bit != 0);
  shadow_prefix_ = shadow_prefix_ * 2U + static_cast<std::uint32_t>(bit != 0);
  if (shadow_prefix_ >= 256U) {
    shadow_prev_byte_ = shadow_prefix_ - 256U;
    shadow_prefix_ = 1U;
  }
  ++shadow_block_bits_;
  if (shadow_block_bits_ == 8ULL * 1000000ULL && shadow_trace_) {
    std::fprintf(shadow_trace_, "%llu\t%.9f\t%.9f\t%.9f\n",
        shadow_block_index_ * 1000000ULL,
        shadow_mixed_loss_ / 1000000.0,
        shadow_candidate_loss_ / 1000000.0,
        (shadow_mixed_loss_ - shadow_candidate_loss_) / 1000000.0);
    std::fflush(shadow_trace_);
    ++shadow_block_index_;
    shadow_block_bits_ = 0;
    shadow_mixed_loss_ = 0.0;
    shadow_candidate_loss_ = 0.0;
  }
#endif

  while (((x1_^x2_) & 0xff000000) == 0) {
    WriteByte(x2_ >> 24);
    x1_ <<= 8;
    x2_ = (x2_ << 8) + 255;
  }
#if CMIX_ENTROPY_TRACE
  if ((trace_block_bits_ == 8ULL * 1000000ULL) && trace_) {
    const double bytes = static_cast<double>(trace_block_bits_) / 8.0;
    std::fprintf(trace_, "%llu\t%llu\t%.9f\t%zu\n",
        trace_block_index_ * 1000000ULL, trace_bits_,
        trace_block_loss_ / bytes, out_.size());
    std::fflush(trace_);
    ++trace_block_index_;
    trace_block_bits_ = 0;
    trace_block_loss_ = 0.0;
  }
#endif
}

void Encoder::EncodeRawBit(int bit, unsigned int p) {
  if (p < 1) p = 1;
  if (p > 65534) p = 65534;
  const unsigned int xmid = x1_ + ((x2_ - x1_) >> 16) * p +
      (((x2_ - x1_) & 0xffff) * p >> 16);
  if (bit) {
    x2_ = xmid;
  } else {
    x1_ = xmid + 1;
  }

  while (((x1_^x2_) & 0xff000000) == 0) {
    WriteByte(x2_ >> 24);
    x1_ <<= 8;
    x2_ = (x2_ << 8) + 255;
  }
}

void Encoder::ObserveKnownBit(int bit) {
  p_->Predict();
  p_->Perceive(bit);
}

void Encoder::ObserveKnownByte(unsigned int byte) {
  for (int bit = 7; bit >= 0; --bit) {
    ObserveKnownBit((byte >> bit) & 1);
  }
}

void Encoder::BeginTraceByte(unsigned long long offset, unsigned int actual_byte,
    unsigned int prev4) {
  (void)offset;
  (void)actual_byte;
  (void)prev4;
}

void Encoder::EndTraceByte() {}

void Encoder::Flush() {
  while (((x1_^x2_) & 0xff000000) == 0) {
    WriteByte(x2_ >> 24);
    x1_ <<= 8;
    x2_ = (x2_ << 8) + 255;
  }
  WriteByte(x2_ >> 24);

  auto* data = reinterpret_cast<const char*>(out_.data());
  os_->write(data, out_.size());
  p_->TraceFlush();
#if CMIX_ENTROPY_TRACE
  if (trace_ && trace_block_bits_ != 0) {
    const double bytes = static_cast<double>(trace_block_bits_) / 8.0;
    std::fprintf(trace_, "%llu\t%llu\t%.9f\t%zu\n",
        trace_block_index_ * 1000000ULL, trace_bits_,
        trace_block_loss_ / bytes, out_.size());
  }
  if (trace_) {
    std::fclose(trace_);
    trace_ = nullptr;
  }
#endif
#if CMIX_SHADOW_TRACE
  if (shadow_trace_) {
    if (shadow_block_bits_ != 0) {
      const double bytes = static_cast<double>(shadow_block_bits_) / 8.0;
      std::fprintf(shadow_trace_, "%llu\t%.9f\t%.9f\t%.9f\n",
          shadow_block_index_ * 1000000ULL,
          shadow_mixed_loss_ / bytes * 8.0,
          shadow_candidate_loss_ / bytes * 8.0,
          (shadow_mixed_loss_ - shadow_candidate_loss_) / bytes * 8.0);
    }
    std::fclose(shadow_trace_);
    shadow_trace_ = nullptr;
  }
#endif
}
