#ifndef PPMD_H
#define PPMD_H

#include "byte-model.h"

#include <array>
#include <memory>
#include <vector>

#ifndef CMIX_PPMD_PRECOMPUTE_PATHS
#define CMIX_PPMD_PRECOMPUTE_PATHS 1
#endif

#ifndef CMIX_FAST_BYTE_MODEL
#define CMIX_FAST_BYTE_MODEL 0
#endif

#ifndef CMIX_PPMD_ENABLED
#define CMIX_PPMD_ENABLED 1
#endif

#ifndef CMIX_PPMD_UPDATE_PERIOD
#define CMIX_PPMD_UPDATE_PERIOD 1
#endif

namespace PPMD {

struct ppmd_Model;

class PPMD : public ByteModel {
 public:
  PPMD(int order, int memory, const unsigned int& bit_context,
      const std::vector<bool>& vocab);
  ~PPMD();
  std::valarray<float>& Predict();
  void Perceive(int bit);
  void ByteUpdate();
 private:
  struct DisabledBytePath {
    unsigned char value;
    std::array<unsigned char, 8> node;
    std::array<unsigned char, 8> bit;
  };

#if CMIX_PPMD_ENABLED || CMIX_FAST_BYTE_MODEL
  const unsigned int& byte_;
#endif
  std::unique_ptr<ppmd_Model> ppmd_model_;
  std::valarray<int> byte_map_;
  std::array<unsigned int, 256> tree_zero_;
  std::array<unsigned int, 256> tree_total_;
  std::vector<unsigned char> disabled_bytes_;
  std::vector<DisabledBytePath> disabled_paths_;
#if CMIX_PPMD_ENABLED
  unsigned int tree_context_ = 1;
#if CMIX_PPMD_UPDATE_PERIOD > 1
  unsigned long long update_counter_ = 0;
#endif
#endif
  bool vocab_full_ = false;
#if CMIX_FAST_BYTE_MODEL
  // Compact decoder-synchronized order-1 byte model used by the fast build.
  // Counts are initialized to one for every permitted byte and updated after
  // each completed byte; no static trained data is embedded in the binary.
  std::array<std::array<unsigned short, 256>, 256> fast_counts_{};
  std::array<unsigned int, 256> fast_totals_{};
  unsigned int fast_previous_byte_ = 0;
#endif
};

} // namespace PPMD

#endif
