#pragma once

#include <expected>
#include <ptxsim/arith/error.hpp>
#include <ptxsim/arith/types.hpp>

namespace ptxsim::arith {
struct nan_policy {
  bool quiet_signaling_nan = true;
  bool preserve_payload = true;
  bool preserve_sign = true;
  bool canonicalize = false;
};
struct approximation_profile {
  int model = 0;
  int target_family = 0;
};
struct tensor_arithmetic_profile {
  bool deterministic = true;
  bool model_dependent = true;
};
enum class tf32_encoding_model { f32_top_19_bits, unsupported };
struct tf32_encoding_profile {
  // Model-dependent: this profile models the common F32-carried TF32 form;
  // PTX does not define it as a universal ISA encoding.
  tf32_encoding_model model = tf32_encoding_model::f32_top_19_bits;
};
struct model_profile {
  nan_policy nan{};
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
