#pragma once

#include <string_view>
#include <ptxsim/common/ids.hpp>

namespace ptxsim::exec_ir {
namespace detail {
/** @brief Dense identity domain for a complete owned storage declaration. */
struct StorageIdTag {
  /** @brief Diagnostic prefix; identities do not contain source spellings. */
  static constexpr std::string_view prefix = "storage";
};
/** @brief Dense identity domain for module and entry opaque resources. */
struct ResourceIdTag {
  /** @brief Diagnostic prefix for frontend-independent resources. */
  static constexpr std::string_view prefix = "resource";
};
/** @brief Dense identity domain for owned parameter declarations. */
struct ParameterIdTag {
  /** @brief Diagnostic prefix for declaration-role-preserving parameters. */
  static constexpr std::string_view prefix = "parameter";
};
/** @brief Dense identity domain for indirect-call prototype declarations. */
struct PrototypeIdTag {
  /** @brief Diagnostic prefix for an owned callable prototype. */
  static constexpr std::string_view prefix = "prototype";
};
/** @brief Dense identity domain for ordered callable target sets. */
struct CallTargetSetIdTag {
  /** @brief Diagnostic prefix for callable target metadata. */
  static constexpr std::string_view prefix = "call-target-set";
};
/** @brief Dense identity domain for function-owned local branch target sets. */
struct BranchTargetSetIdTag {
  /** @brief Diagnostic prefix for local-PC target metadata. */
  static constexpr std::string_view prefix = "branch-target-set";
};
/** @brief Dense identity domain for sparse callable storage tables. */
struct CallTableIdTag {
  /** @brief Diagnostic prefix for a table and its exact ordered slots. */
  static constexpr std::string_view prefix = "call-table";
};
}  // namespace detail

/** @brief Index into the immutable program's full storage declaration table. */
using StorageId = common::detail::Id<detail::StorageIdTag>;
/** @brief Index into the immutable program's opaque resource table. */
using ResourceId = common::detail::Id<detail::ResourceIdTag>;
/** @brief Index into the immutable program's parameter declaration table. */
using ParameterId = common::detail::Id<detail::ParameterIdTag>;
/** @brief Index into the immutable program's indirect-call prototype table. */
using PrototypeId = common::detail::Id<detail::PrototypeIdTag>;
/** @brief Index into the immutable program's callable target-set table. */
using CallTargetSetId = common::detail::Id<detail::CallTargetSetIdTag>;
/** @brief Index into the immutable program's local branch target-set table. */
using BranchTargetSetId = common::detail::Id<detail::BranchTargetSetIdTag>;
/** @brief Index into the immutable program's sparse call-table metadata. */
using CallTableId = common::detail::Id<detail::CallTableIdTag>;
}  // namespace ptxsim::exec_ir
