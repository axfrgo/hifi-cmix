#include "direct.h"

Direct::Direct(const unsigned long long& byte_context,
    const unsigned int& bit_context, int limit, float delta, int size) :
    byte_context_(byte_context), bit_context_(bit_context), limit_(limit),
    delta_(delta), divisor_(1.0 / (limit + delta)),
    predictions_(size, std::array<float, 256>()),
    counts_(size, std::array<unsigned char, 256>()) {
  for (int i = 0; i < size; ++i) {
    predictions_[i].fill(0.5);
    counts_[i].fill(0);
  }
}

const std::valarray<float>& Direct::Predict() const {
  outputs_[0] = predictions_[byte_context_][bit_context_];
  return outputs_;
}

void Direct::Perceive(int bit) {
  float divisor = divisor_;
  if (counts_[byte_context_][bit_context_] < limit_) {
    ++counts_[byte_context_][bit_context_];
    divisor = 1.0 / (counts_[byte_context_][bit_context_] + delta_);
  }
  predictions_[byte_context_][bit_context_] +=
      (bit - predictions_[byte_context_][bit_context_]) * divisor;
}

HashedDirect::HashedDirect(
    const std::vector<unsigned long long>& recent_bytes,
    const unsigned int& bit_context, unsigned int table_bits) :
    recent_bytes_(recent_bytes), bit_context_(bit_context),
    predictions_(static_cast<size_t>(1) << table_bits, 0.5f),
    counts_(static_cast<size_t>(1) << table_bits, 0) {}

unsigned int HashedDirect::Index() const {
  // A fixed-width avalanche hash avoids expensive variable-size context
  // storage while retaining the three-byte history and bit prefix.
  std::uint32_t h = 2166136261u;
  h = (h ^ static_cast<std::uint32_t>(recent_bytes_[0])) * 16777619u;
  h = (h ^ static_cast<std::uint32_t>(recent_bytes_[1])) * 16777619u;
  h = (h ^ static_cast<std::uint32_t>(recent_bytes_[2])) * 16777619u;
  h = (h ^ static_cast<std::uint32_t>(bit_context_)) * 16777619u;
  h ^= h >> 16;
  return h & Mask();
}

const std::valarray<float>& HashedDirect::Predict() const {
  outputs_[0] = predictions_[Index()];
  return outputs_;
}

void HashedDirect::Perceive(int bit) {
  const unsigned int index = Index();
  const unsigned int count = counts_[index];
  const float divisor = 1.0f / static_cast<float>(count + 2);
  predictions_[index] += (static_cast<float>(bit) - predictions_[index]) * divisor;
  if (counts_[index] != 255) ++counts_[index];
}

