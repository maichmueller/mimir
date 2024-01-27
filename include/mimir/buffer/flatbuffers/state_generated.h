// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_STATE_MIMIR_H_
#define FLATBUFFERS_GENERATED_STATE_MIMIR_H_

#include "flatbuffers/flatbuffers.h"

// Ensure the included flatbuffers.h is the same version as when this file was
// generated, otherwise it may not be compatible.
static_assert(FLATBUFFERS_VERSION_MAJOR == 23 &&
              FLATBUFFERS_VERSION_MINOR == 5 &&
              FLATBUFFERS_VERSION_REVISION == 26,
             "Non-compatible flatbuffers version included");

namespace mimir {

struct State;
struct StateBuilder;

struct State FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef StateBuilder Builder;
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_ATOMS = 4
  };
  const ::flatbuffers::Vector<uint64_t> *atoms() const {
    return GetPointer<const ::flatbuffers::Vector<uint64_t> *>(VT_ATOMS);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_ATOMS) &&
           verifier.VerifyVector(atoms()) &&
           verifier.EndTable();
  }
};

struct StateBuilder {
  typedef State Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_atoms(::flatbuffers::Offset<::flatbuffers::Vector<uint64_t>> atoms) {
    fbb_.AddOffset(State::VT_ATOMS, atoms);
  }
  explicit StateBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<State> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<State>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<State> CreateState(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    ::flatbuffers::Offset<::flatbuffers::Vector<uint64_t>> atoms = 0) {
  StateBuilder builder_(_fbb);
  builder_.add_atoms(atoms);
  return builder_.Finish();
}

inline ::flatbuffers::Offset<State> CreateStateDirect(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    const std::vector<uint64_t> *atoms = nullptr) {
  auto atoms__ = atoms ? _fbb.CreateVector<uint64_t>(*atoms) : 0;
  return mimir::CreateState(
      _fbb,
      atoms__);
}

inline const mimir::State *GetState(const void *buf) {
  return ::flatbuffers::GetRoot<mimir::State>(buf);
}

inline const mimir::State *GetSizePrefixedState(const void *buf) {
  return ::flatbuffers::GetSizePrefixedRoot<mimir::State>(buf);
}

inline bool VerifyStateBuffer(
    ::flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<mimir::State>(nullptr);
}

inline bool VerifySizePrefixedStateBuffer(
    ::flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<mimir::State>(nullptr);
}

inline void FinishStateBuffer(
    ::flatbuffers::FlatBufferBuilder &fbb,
    ::flatbuffers::Offset<mimir::State> root) {
  fbb.Finish(root);
}

inline void FinishSizePrefixedStateBuffer(
    ::flatbuffers::FlatBufferBuilder &fbb,
    ::flatbuffers::Offset<mimir::State> root) {
  fbb.FinishSizePrefixed(root);
}

}  // namespace mimir

#endif  // FLATBUFFERS_GENERATED_STATE_MIMIR_H_