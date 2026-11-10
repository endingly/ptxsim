#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <ptxsim/exec_ir/owned_operands.hpp>
#include <ptxsim/common/raw_value.hpp>

namespace ptxsim::exec_ir {
/** @brief Physical container width of a modeled type, independent of arithmetic support. */
[[nodiscard]] auto data_type_width(DataType) noexcept -> std::optional<common::RawWidth>;
/** @brief Materialize typed immediate use bits into their physical container. */
[[nodiscard]] auto immediate_value(const Immediate&) noexcept -> std::optional<common::RawValue>;
/** @brief Check output inversion without restricting a predicate read or its slot ID. */
[[nodiscard]] auto valid_predicate_destination(const Predicate&) noexcept -> bool;
/** @brief Check an optional predicate output while preserving a genuine sink. */
[[nodiscard]] auto valid_predicate_destination(const PredicateOrSink&) noexcept -> bool;
/** @brief Check both ordered predicate output inversions. */
[[nodiscard]] auto valid_predicate_destination(const PredicatePair&) noexcept -> bool;
/** @brief Check each present predicate output without resolving register storage. */
[[nodiscard]] auto valid_predicate_destination(const PredicatePairOrSink&) noexcept -> bool;
/** @brief Check the optional predicate portion of a shuffle result. */
[[nodiscard]] auto valid_predicate_destination(const ShflDestination&) noexcept -> bool;
/** @brief Check the optional texture residency output. */
[[nodiscard]] auto valid_predicate_destination(const TextureResult&) noexcept -> bool;
/** @brief Check owned RegisterRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const RegisterRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned RegisterRef. */
[[nodiscard]] auto to_string(const RegisterRef&) -> std::string;
/** @brief Check owned Predicate shape without reading execution state. */
[[nodiscard]] auto valid_operand(const Predicate&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned Predicate. */
[[nodiscard]] auto to_string(const Predicate&) -> std::string;
/** @brief Check owned PredicateOrSink shape without reading execution state. */
[[nodiscard]] auto valid_operand(const PredicateOrSink&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned PredicateOrSink. */
[[nodiscard]] auto to_string(const PredicateOrSink&) -> std::string;
/** @brief Check owned PredicateSource shape without reading execution state. */
[[nodiscard]] auto valid_operand(const PredicateSource&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned PredicateSource. */
[[nodiscard]] auto to_string(const PredicateSource&) -> std::string;
/** @brief Check owned Immediate shape without reading execution state. */
[[nodiscard]] auto valid_operand(const Immediate&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned Immediate. */
[[nodiscard]] auto to_string(const Immediate&) -> std::string;
/** @brief Check owned ScalarOperand shape without reading execution state. */
[[nodiscard]] auto valid_operand(const ScalarOperand&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned ScalarOperand. */
[[nodiscard]] auto to_string(const ScalarOperand&) -> std::string;
/** @brief Check owned RegisterOrSink shape without reading execution state. */
[[nodiscard]] auto valid_operand(const RegisterOrSink&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned RegisterOrSink. */
[[nodiscard]] auto to_string(const RegisterOrSink&) -> std::string;
/** @brief Check owned CvtSource shape without reading execution state. */
[[nodiscard]] auto valid_operand(const CvtSource&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned CvtSource. */
[[nodiscard]] auto to_string(const CvtSource&) -> std::string;
/** @brief Check owned MovSource shape without reading execution state. */
[[nodiscard]] auto valid_operand(const MovSource&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned MovSource. */
[[nodiscard]] auto to_string(const MovSource&) -> std::string;
/** @brief Check owned MovVectorSource shape without reading execution state. */
[[nodiscard]] auto valid_operand(const MovVectorSource&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned MovVectorSource. */
[[nodiscard]] auto to_string(const MovVectorSource&) -> std::string;
/** @brief Check owned CpAsyncSourceControl shape without reading execution state. */
[[nodiscard]] auto valid_operand(const CpAsyncSourceControl&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned CpAsyncSourceControl. */
[[nodiscard]] auto to_string(const CpAsyncSourceControl&) -> std::string;
/** @brief Check owned VectorRegisterRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const VectorRegisterRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned VectorRegisterRef. */
[[nodiscard]] auto to_string(const VectorRegisterRef&) -> std::string;
/** @brief Check owned VectorSpecialRegisterRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const VectorSpecialRegisterRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned VectorSpecialRegisterRef. */
[[nodiscard]] auto to_string(const VectorSpecialRegisterRef&) -> std::string;
/** @brief Check owned BranchTarget shape without reading execution state. */
[[nodiscard]] auto valid_operand(const BranchTarget&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned BranchTarget. */
[[nodiscard]] auto to_string(const BranchTarget&) -> std::string;
/** @brief Check owned SpecialRegisterRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const SpecialRegisterRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned SpecialRegisterRef. */
[[nodiscard]] auto to_string(const SpecialRegisterRef&) -> std::string;
/** @brief Check owned SymbolRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const SymbolRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned SymbolRef. */
[[nodiscard]] auto to_string(const SymbolRef&) -> std::string;
/** @brief Check owned Address shape without reading execution state. */
[[nodiscard]] auto valid_operand(const Address&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned Address. */
[[nodiscard]] auto to_string(const Address&) -> std::string;
/** @brief Check owned RegisterVector shape without reading execution state. */
[[nodiscard]] auto valid_operand(const RegisterVector&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned RegisterVector. */
[[nodiscard]] auto to_string(const RegisterVector&) -> std::string;
/** @brief Check owned ValueVector shape without reading execution state. */
[[nodiscard]] auto valid_operand(const ValueVector&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned ValueVector. */
[[nodiscard]] auto to_string(const ValueVector&) -> std::string;
/** @brief Check owned TensorCoordinate shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TensorCoordinate&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TensorCoordinate. */
[[nodiscard]] auto to_string(const TensorCoordinate&) -> std::string;
/** @brief Check owned TensorIm2colInfo shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TensorIm2colInfo&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TensorIm2colInfo. */
[[nodiscard]] auto to_string(const TensorIm2colInfo&) -> std::string;
/** @brief Check owned TensorOperand shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TensorOperand&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TensorOperand. */
[[nodiscard]] auto to_string(const TensorOperand&) -> std::string;
/** @brief Check owned FabricHandle shape without reading execution state. */
[[nodiscard]] auto valid_operand(const FabricHandle&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned FabricHandle. */
[[nodiscard]] auto to_string(const FabricHandle&) -> std::string;
/** @brief Check owned SurfaceAccess shape without reading execution state. */
[[nodiscard]] auto valid_operand(const SurfaceAccess&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned SurfaceAccess. */
[[nodiscard]] auto to_string(const SurfaceAccess&) -> std::string;
/** @brief Check owned SurfaceQueryResource shape without reading execution state. */
[[nodiscard]] auto valid_operand(const SurfaceQueryResource&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned SurfaceQueryResource. */
[[nodiscard]] auto to_string(const SurfaceQueryResource&) -> std::string;
/** @brief Check owned StackToken shape without reading execution state. */
[[nodiscard]] auto valid_operand(const StackToken&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned StackToken. */
[[nodiscard]] auto to_string(const StackToken&) -> std::string;
/** @brief Check owned LocalAllocationResult shape without reading execution state. */
[[nodiscard]] auto valid_operand(const LocalAllocationResult&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned LocalAllocationResult. */
[[nodiscard]] auto to_string(const LocalAllocationResult&) -> std::string;
/** @brief Check owned VideoOperand shape without reading execution state. */
[[nodiscard]] auto valid_operand(const VideoOperand&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned VideoOperand. */
[[nodiscard]] auto to_string(const VideoOperand&) -> std::string;
/** @brief Check owned TextureAccess shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TextureAccess&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TextureAccess. */
[[nodiscard]] auto to_string(const TextureAccess&) -> std::string;
/** @brief Check owned TextureQueryResource shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TextureQueryResource&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TextureQueryResource. */
[[nodiscard]] auto to_string(const TextureQueryResource&) -> std::string;
/** @brief Check owned TextureResult shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TextureResult&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TextureResult. */
[[nodiscard]] auto to_string(const TextureResult&) -> std::string;
/** @brief Check owned TensorMemoryAddress shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TensorMemoryAddress&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TensorMemoryAddress. */
[[nodiscard]] auto to_string(const TensorMemoryAddress&) -> std::string;
/** @brief Check owned TcgenHalfSplitOffset shape without reading execution state. */
[[nodiscard]] auto valid_operand(const TcgenHalfSplitOffset&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned TcgenHalfSplitOffset. */
[[nodiscard]] auto to_string(const TcgenHalfSplitOffset&) -> std::string;
/** @brief Check owned MatrixScaleSelector shape without reading execution state. */
[[nodiscard]] auto valid_operand(const MatrixScaleSelector&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned MatrixScaleSelector. */
[[nodiscard]] auto to_string(const MatrixScaleSelector&) -> std::string;
/** @brief Check owned SharedMatrixDescriptor shape without reading execution state. */
[[nodiscard]] auto valid_operand(const SharedMatrixDescriptor&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned SharedMatrixDescriptor. */
[[nodiscard]] auto to_string(const SharedMatrixDescriptor&) -> std::string;
/** @brief Check owned FunctionRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const FunctionRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned FunctionRef. */
[[nodiscard]] auto to_string(const FunctionRef&) -> std::string;
/** @brief Check owned IndirectCallee shape without reading execution state. */
[[nodiscard]] auto valid_operand(const IndirectCallee&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned IndirectCallee. */
[[nodiscard]] auto to_string(const IndirectCallee&) -> std::string;
/** @brief Check owned BranchTargetSet shape without reading execution state. */
[[nodiscard]] auto valid_operand(const BranchTargetSet&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned BranchTargetSet. */
[[nodiscard]] auto to_string(const BranchTargetSet&) -> std::string;
/** @brief Check owned CallParameterRef shape without reading execution state. */
[[nodiscard]] auto valid_operand(const CallParameterRef&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned CallParameterRef. */
[[nodiscard]] auto to_string(const CallParameterRef&) -> std::string;
/** @brief Check owned CallArguments shape without reading execution state. */
[[nodiscard]] auto valid_operand(const CallArguments&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned CallArguments. */
[[nodiscard]] auto to_string(const CallArguments&) -> std::string;
/** @brief Check owned ShflDestination shape without reading execution state. */
[[nodiscard]] auto valid_operand(const ShflDestination&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned ShflDestination. */
[[nodiscard]] auto to_string(const ShflDestination&) -> std::string;
/** @brief Check owned PredicatePair shape without reading execution state. */
[[nodiscard]] auto valid_operand(const PredicatePair&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned PredicatePair. */
[[nodiscard]] auto to_string(const PredicatePair&) -> std::string;
/** @brief Check owned PredicatePairOrSink shape without reading execution state. */
[[nodiscard]] auto valid_operand(const PredicatePairOrSink&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned PredicatePairOrSink. */
[[nodiscard]] auto to_string(const PredicatePairOrSink&) -> std::string;
/** @brief Check owned MbarrierStateToken shape without reading execution state. */
[[nodiscard]] auto valid_operand(const MbarrierStateToken&) noexcept -> bool;
/** @brief Format every execution-relevant field of an owned MbarrierStateToken. */
[[nodiscard]] auto to_string(const MbarrierStateToken&) -> std::string;
/** @brief Return the concrete lane count of a checked vector arity control. */
[[nodiscard]] auto vector_arity_size(VectorArity) noexcept -> std::size_t;
/** @brief Return actual list length; non-list alternatives remain absent. */
[[nodiscard]] auto operand_cardinality(const RegisterVector&) noexcept -> std::optional<std::size_t>;
/** @brief Return actual list length; non-list alternatives remain absent. */
[[nodiscard]] auto operand_cardinality(const ValueVector&) noexcept -> std::optional<std::size_t>;
/** @brief Return actual list length; non-list alternatives remain absent. */
[[nodiscard]] auto operand_cardinality(const MovVectorSource&) noexcept -> std::optional<std::size_t>;
/** @brief Return actual list length; non-list alternatives remain absent. */
[[nodiscard]] auto operand_cardinality(const TensorCoordinate&) noexcept -> std::optional<std::size_t>;
/** @brief Return actual list length; non-list alternatives remain absent. */
[[nodiscard]] auto operand_cardinality(const TensorIm2colInfo&) noexcept -> std::optional<std::size_t>;
/** @brief Return actual list length; non-list alternatives remain absent. */
[[nodiscard]] auto operand_cardinality(const CallArguments&) noexcept -> std::optional<std::size_t>;
/** @brief Count logical tensor coordinates without evaluating their carriers. */
[[nodiscard]] auto operand_cardinality(const TensorOperand&) noexcept -> std::optional<std::size_t>;
/** @brief Count data destinations separately from the optional residency output. */
[[nodiscard]] auto operand_cardinality(const TextureResult&) noexcept -> std::optional<std::size_t>;
/** @brief Report the two independently typed matrix scale selectors. */
[[nodiscard]] auto operand_cardinality(const MatrixScaleSelector&) noexcept -> std::optional<std::size_t>;
/** @brief Check canonical/shared space together with its actual atomic qualifier. */
[[nodiscard]] auto atomic_space_matches(AddressSpace, AtomicAddressQualifier) noexcept -> bool;
/** @brief Check canonical parameter space together with its selected address role. */
[[nodiscard]] auto parameter_space_matches(AddressSpace, ParameterAddressQualifier) noexcept -> bool;
}  // namespace ptxsim::exec_ir
