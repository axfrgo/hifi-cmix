#include "byte-mixer.h"

ByteMixer::ByteMixer(unsigned int num_models, const unsigned int& bit_context,
    const std::vector<bool>& vocab, unsigned int vocab_size, Lstm* lstm) :
    ByteModel(vocab), lstm_(lstm), byte_(bit_context), byte_map_(0, 256),
    inputs_(0.0, vocab_size), num_models_(num_models), vocab_size_(vocab_size),
    offset_(0) {
  for (int i = 0; i < 256; ++i) {
    byte_map_[i] = offset_;
    if (vocab_[i]) ++offset_;
  }
  offset_ = 0;
}

void ByteMixer::SetInput(int index, float val) {
  if (!vocab_[index]) return;
  inputs_[offset_] += val;
  ++offset_;
  if (offset_ == vocab_size_) offset_ = 0;
}

void ByteMixer::ByteUpdate() {
#if CMIX_FAST_BYTE_MIXER
  // Fast branch: use the already-computed byte-model mixture directly. This
  // removes the recurrent LSTM update while keeping the same causal interface
  // and decoder-synchronized byte boundary.
  const float total = inputs_.sum();
  offset_ = 0;
  for (int i = 0; i < 256; ++i) {
    if (vocab_[i]) {
      probs_[i] = total > 0.0f ? inputs_[offset_] / total :
          1.0f / static_cast<float>(vocab_size_);
      ++offset_;
    } else {
      probs_[i] = 0.0f;
    }
  }
  inputs_ = 0;
  offset_ = 0;
  ByteModel::ByteUpdate();
  return;
#else
  inputs_ *= 2 / num_models_;
  lstm_->SetInput(inputs_);
  inputs_ = 0;
  const auto& output = lstm_->Perceive(byte_map_[byte_]);
  offset_ = 0;
  for (int i = 0; i < 256; ++i) {
    if (vocab_[i]) {
      probs_[i] = output[offset_];
      ++offset_;
    } else {
      probs_[i] = 0;
    }
  }
  offset_ = 0;
  ByteModel::ByteUpdate();
#endif
}
