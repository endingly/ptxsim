#pragma once

#include <cassert>
#include <cstddef>

#include <ptxsim/execution_model/ids.hpp>
#include <sul/dynamic_bitset.hpp>

namespace ptxsim::execution_model {

/**
 * @brief A set of lanes within one warp.
 *
 * LaneMask is the canonical representation of a lane set in the execution
 * model. It is used for partial-warps, issue groups, synchronization
 * participants, arrivals, and other warp-scoped operations.
 *
 * The underlying bitset representation is intentionally hidden so that the
 * implementation may be changed without affecting users of the execution
 * model.
 *
 * Bit position N corresponds to LaneId{N}.
 */
class LaneMask final {
 public:
  /**
   * @brief Construct an empty, zero-width lane mask.
   *
   * A zero-width mask is primarily useful as a default-constructed value.
   * Normal warp-scoped masks should be constructed with an explicit lane
   * count.
   */
  LaneMask() = default;

  /**
   * @brief Construct an empty lane mask with the specified width.
   *
   * All lanes are initially unset.
   */
  explicit LaneMask(std::size_t lane_count) {
    bits_.resize(lane_count);

    for (std::size_t i = 0; i < lane_count; ++i) {
      bits_[i] = false;
    }
  }

  /**
   * @brief Return the number of lanes represented by this mask.
   */
  [[nodiscard]]
  std::size_t size() const noexcept {
    return bits_.size();
  }

  /**
   * @brief Return true when the mask contains no lane positions.
   *
   * This checks the width of the mask rather than whether all bits are zero.
   */
  [[nodiscard]]
  bool empty() const noexcept {
    return size() == 0;
  }

  /**
   * @brief Test whether a lane is present in the set.
   */
  [[nodiscard]]
  bool test(LaneId lane) const noexcept {
    assert(lane.value < size());
    return bits_[lane.value];
  }

  /**
   * @brief Add a lane to the set.
   */
  void set(LaneId lane) noexcept {
    assert(lane.value < size());
    bits_[lane.value] = true;
  }

  /**
   * @brief Remove a lane from the set.
   */
  void reset(LaneId lane) noexcept {
    assert(lane.value < size());
    bits_[lane.value] = false;
  }

  /**
   * @brief Set all lane bits to zero.
   */
  void clear() noexcept {
    for (std::size_t i = 0; i < size(); ++i) {
      bits_[i] = false;
    }
  }

  /**
   * @brief Return true if at least one lane is present.
   */
  [[nodiscard]]
  bool any() const noexcept {
    for (std::size_t i = 0; i < size(); ++i) {
      if (bits_[i]) {
        return true;
      }
    }

    return false;
  }

  /**
   * @brief Return true if no lanes are present.
   */
  [[nodiscard]]
  bool none() const noexcept {
    return !any();
  }

  /**
   * @brief Return the number of lanes present in the set.
   */
  [[nodiscard]]
  std::size_t count() const noexcept {
    std::size_t result = 0;

    for (std::size_t i = 0; i < size(); ++i) {
      if (bits_[i]) {
        ++result;
      }
    }

    return result;
  }

  /**
   * @brief Return true if this mask contains every lane in @p other.
   *
   * Both masks must use the same lane width.
   */
  [[nodiscard]]
  bool contains(const LaneMask& other) const noexcept {
    assert(size() == other.size());

    for (std::size_t i = 0; i < size(); ++i) {
      if (other.bits_[i] && !bits_[i]) {
        return false;
      }
    }

    return true;
  }

  /**
   * @brief Merge all lanes from @p other into this mask.
   */
  void merge(const LaneMask& other) noexcept {
    assert(size() == other.size());

    for (std::size_t i = 0; i < size(); ++i) {
      bits_[i] = bits_[i] || other.bits_[i];
    }
  }

  /**
   * @brief Keep only lanes also present in @p other.
   */
  void intersect(const LaneMask& other) noexcept {
    assert(size() == other.size());

    for (std::size_t i = 0; i < size(); ++i) {
      bits_[i] = bits_[i] && other.bits_[i];
    }
  }

  /**
   * @brief Remove all lanes present in @p other.
   */
  void subtract(const LaneMask& other) noexcept {
    assert(size() == other.size());

    for (std::size_t i = 0; i < size(); ++i) {
      if (other.bits_[i]) {
        bits_[i] = false;
      }
    }
  }

  [[nodiscard]]
  friend bool operator==(const LaneMask& lhs, const LaneMask& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i) {
      if (lhs.bits_[i] != rhs.bits_[i]) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]]
  friend bool operator!=(const LaneMask& lhs, const LaneMask& rhs) noexcept {
    return !(lhs == rhs);
  }

  /**
   * @brief Return the union of two lane masks.
   */
  [[nodiscard]]
  friend LaneMask operator|(LaneMask lhs, const LaneMask& rhs) noexcept {
    lhs.merge(rhs);
    return lhs;
  }

  /**
   * @brief Return the intersection of two lane masks.
   */
  [[nodiscard]]
  friend LaneMask operator&(LaneMask lhs, const LaneMask& rhs) noexcept {
    lhs.intersect(rhs);
    return lhs;
  }

 private:
  sul::dynamic_bitset<> bits_;
};

}  // namespace ptxsim::execution_model