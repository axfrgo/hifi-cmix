#ifndef DIRECT_H
#define DIRECT_H

#include "model.h"

#include <vector>
#include <array>
#include <cstdint>

class Direct : public Model {
 public:
  Direct(const unsigned long long& byte_context,
      const unsigned int& bit_context, int limit, float delta, int size);
  const std::valarray<float>& Predict() const;
  void Perceive(int bit);
  void ByteUpdate() {};

 private:
  const unsigned long long& byte_context_;
  const unsigned int& bit_context_;
  int limit_;
  float delta_, divisor_;
  std::vector<std::array<float, 256>> predictions_;
  std::vector<std::array<unsigned char, 256>> counts_;
};

// Bounded hashed n-gram bit predictor. The hash combines the previous three
// completed bytes with the current byte prefix, so encoder and decoder see
// identical state without storing a context dictionary in the archive.
class HashedDirect : public Model {
 public:
  HashedDirect(const std::vector<unsigned long long>& recent_bytes,
      const unsigned int& bit_context, unsigned int table_bits = 20);
  const std::valarray<float>& Predict() const;
  void Perceive(int bit);
  void ByteUpdate() {}

 private:
  const std::vector<unsigned long long>& recent_bytes_;
  const unsigned int& bit_context_;
  std::vector<float> predictions_;
  std::vector<unsigned char> counts_;
  unsigned int Mask() const { return static_cast<unsigned int>(predictions_.size() - 1); }
  unsigned int Index() const;
};

#endif

