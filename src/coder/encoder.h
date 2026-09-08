#ifndef ENCODER_H
#define ENCODER_H

#include <fstream>
#include <vector>
#include <array>
#include <cstdint>
#include <cstdio>

#include "../predictor.h"

#ifndef CMIX_SHADOW_TRACE
#define CMIX_SHADOW_TRACE 0
#endif

class Encoder {
 public:
  Encoder(std::ofstream* os, Predictor* p);
  void Encode(int bit);
  void EncodeRawBit(int bit, unsigned int p = 32768);
  void ObserveKnownBit(int bit);
  void ObserveKnownByte(unsigned int byte);
  void BeginTraceByte(unsigned long long offset, unsigned int actual_byte,
      unsigned int prev4);
  void EndTraceByte();
  void Flush();
  size_t OutputSize() { return out_.size();}
 private:
  void WriteByte(unsigned int byte);
  unsigned int Discretize(float p);

  std::vector<char> out_;
  std::ofstream* os_;
  unsigned int x1_, x2_;
  Predictor* p_;
#if CMIX_ENTROPY_TRACE
  std::FILE* trace_;
  std::uint64_t trace_bits_;
  std::uint64_t trace_block_bits_;
  double trace_block_loss_;
  unsigned long long trace_block_index_;
#endif
#if CMIX_SHADOW_TRACE
  // Decoder-causal diagnostic expert: previous completed byte plus current
  // byte prefix. It is never supplied to the arithmetic coder.
  std::array<std::uint32_t, 65536> shadow_ones_{};
  std::array<std::uint32_t, 65536> shadow_totals_{};
  std::uint32_t shadow_prev_byte_ = 0;
  std::uint32_t shadow_prefix_ = 1;
  std::uint64_t shadow_block_bits_ = 0;
  std::uint64_t shadow_block_index_ = 0;
  double shadow_mixed_loss_ = 0.0;
  double shadow_candidate_loss_ = 0.0;
  std::FILE* shadow_trace_ = nullptr;
#endif
};
#endif
