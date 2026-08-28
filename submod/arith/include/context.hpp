#pragma once

#include <expected>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/types.hpp>

namespace ptxsim::arith {
// The public profile is intentionally a closed, versioned contract.  PTX
// specifies approximate instructions in terms of accuracy/corner-case bounds,
// not a portable bit-identical hardware implementation.
enum class ptx_numeric_revision { v9_3 };
enum class approximation_model { ptx_9_3_reference, unavailable };
enum class approximation_provenance { model_dependent_reference };
struct approximation_profile {
  ptx_numeric_revision revision = ptx_numeric_revision::v9_3;
  approximation_model model = approximation_model::ptx_9_3_reference;
  approximation_provenance provenance =
      approximation_provenance::model_dependent_reference;
};
// Tensor arithmetic has the same closed-profile rule as approximate scalar
// operations.  Booleans cannot represent an unknown model or provenance, and
// would let callers accidentally claim that a model-dependent result is not
// model dependent.
enum class tensor_model { ptx_9_3_reference, unavailable };
enum class tensor_provenance { model_dependent_reference };
struct tensor_arithmetic_profile {
  ptx_numeric_revision revision = ptx_numeric_revision::v9_3;
  tensor_model model = tensor_model::ptx_9_3_reference;
  tensor_provenance provenance = tensor_provenance::model_dependent_reference;
};
enum class tf32_encoding_model { f32_top_19_bits, unsupported };
struct tf32_encoding_profile {
  // Model-dependent: this profile models the common F32-carried TF32 form;
  // PTX does not define it as a universal ISA encoding.
  tf32_encoding_model model = tf32_encoding_model::f32_top_19_bits;
};
struct model_profile {
  approximation_profile approximation{};
  tensor_arithmetic_profile tensor{};
  tf32_encoding_profile tf32{};
  static constexpr model_profile ptx_9_3_reference() { return {}; }
};
class context {
 public:
  constexpr explicit context(
      model_profile p = model_profile::ptx_9_3_reference())
      : profile_(p) {}
  [[nodiscard]] constexpr const model_profile& profile() const noexcept {
    return profile_;
  }

 private:
  model_profile profile_;
};

[[nodiscard]] std::expected<bits32_t, arithmetic_error> encode(
    tfloat32_t value, const tf32_encoding_profile& profile) noexcept;
[[nodiscard]] std::expected<tfloat32_t, arithmetic_error> decode_tf32(
    bits32_t bits, const tf32_encoding_profile& profile) noexcept;
}  // namespace ptxsim::arith
