#include <ptxsim/inst_execute_engine/inst_execute_engine.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <utility>

#include <ptxsim/arith/controls.hpp>
#include <ptxsim/arith/scalar.hpp>
#include <ptxsim/memory/register/register_view.hpp>

namespace ptxsim::inst_execute_engine {
namespace {

struct PreparedWrite {
  memory::RegisterView registers;
  common::RegisterSlot destination;
  common::RawValue value;
};

/**
 * @brief One validated scalar store retained until the lane's commit turn.
 *
 * The view is a non-owning manager capability; its weak lifetime is checked
 * again by `write` during commit.
 */
struct PreparedMemoryWrite {
  /** Target resource view selected during prepare. */
  memory::AddressSpaceView space;
  /** Byte offset within `space`, not a generic virtual address. */
  memory::Address address;
  /** Four bytes serialized in PTX little-endian order. */
  std::array<std::byte, 4> value;
};

struct ExitControl {};

using PreparedControl = std::variant<common::ProgramCounter, ExitControl>;

struct PreparedEffect {
  std::optional<PreparedWrite> write;
  /** Present only for a validated scalar store; committed after register writes. */
  std::optional<PreparedMemoryWrite> memory_write;
  PreparedControl control;
};

struct PreparedLane {
  execution_model::Thread* thread;
  PreparedEffect effect;
};

/**
 * @brief Lazily resolves one lane's register frame and byte-addressable view.
 *
 * It owns no runtime storage; cached views remain valid only while the runtime
 * resources survive their normal manager lifetime checks.
 */
class LaneResourceResolver final {
 public:
  /** @brief Associates resolution with one issued thread and function frame. */
  LaneResourceResolver(runtime::LaunchRuntime& runtime,
                       execution_model::Thread& thread,
                       common::FunctionId function) noexcept
      : runtime_(runtime), thread_(thread), function_(function) {}

  /** @brief Return the lane's cached register view or its binding fault. */
  auto resolve()
      -> std::expected<std::reference_wrapper<const memory::RegisterView>,
                       LaneFaultCause> {
    if (!registers_) {
      const auto frame = runtime_.register_frame(thread_.id(), function_);
      if (!frame) {
        return std::unexpected(LaneFaultCause{frame.error()});
      }
      const auto view = runtime_.registers().view(*frame);
      if (!view) {
        return std::unexpected(LaneFaultCause{view.error()});
      }
      registers_ = *view;
    }
    return std::cref(*registers_);
  }

  /**
   * @brief Read a b64 address slot and bind it to a region-relative view.
   *
   * Generic addresses use the lane's execution address context; explicit
   * global addresses are already offsets in the runtime global region.
   */
  auto resolve_memory(exec_ir::AddressSpace space, common::RegisterSlot address)
      -> std::expected<std::pair<memory::AddressSpaceView, memory::Address>,
                       LaneFaultCause> {
    const auto registers = resolve();
    if (!registers) {
      return std::unexpected(registers.error());
    }
    const auto raw = registers->get().read(address);
    if (!raw) {
      return std::unexpected(LaneFaultCause{raw.error()});
    }
    const auto value = raw->as_b64();
    if (!value) {
      return std::unexpected(LaneFaultCause{value.error()});
    }
    switch (space) {
      case exec_ir::AddressSpace::global: {
        const auto global = runtime_.global();
        if (!global) {
          return std::unexpected(LaneFaultCause{global.error()});
        }
        const auto view = runtime_.address_spaces().view(*global);
        if (!view) {
          return std::unexpected(LaneFaultCause{view.error()});
        }
        return std::pair{*view, memory::Address{*value}};
      }
      case exec_ir::AddressSpace::generic: {
        const auto context = runtime_.address_context(thread_.id(), function_);
        if (!context) {
          return std::unexpected(LaneFaultCause{context.error()});
        }
        const auto resolved =
            memory::resolve(memory::GenericAddress{*value}, *context);
        if (!resolved) {
          return std::unexpected(LaneFaultCause{resolved.error()});
        }
        const auto view = std::visit(
            [this]<memory::AddressSpaceHandleType Handle>(const Handle& handle)
                -> std::expected<memory::AddressSpaceView,
                                 memory::AddressSpaceError> {
              return runtime_.address_spaces().view(handle);
            },
            resolved->resource);
        if (!view) {
          return std::unexpected(LaneFaultCause{view.error()});
        }
        return std::pair{*view, resolved->region_address};
      }
    }
    return std::unexpected(LaneFaultCause{memory::AddressResolutionError{
        memory::AddressResolutionErrorCode::unmapped_address,
        memory::GenericAddress{*value}, std::nullopt}});
  }

 private:
  /** Non-owning launch resource owner for the current executor call. */
  runtime::LaunchRuntime& runtime_;
  /** Non-owning lane whose thread/CTA bindings select address resources. */
  execution_model::Thread& thread_;
  /** Static function layout identity used for frame and local bindings. */
  common::FunctionId function_;
  /** Cached non-owning register view, populated only after a successful bind. */
  std::optional<memory::RegisterView> registers_;
};

auto step_error(StepErrorCode code,
                std::optional<execution_model::LaneId> lane = std::nullopt)
    -> std::unexpected<StepError> {
  return std::unexpected(StepError{code, lane});
}

auto b32_operand(const memory::RegisterView& registers,
                 const exec_ir::B32Operand& operand)
    -> std::expected<std::uint32_t, LaneFaultCause> {
  if (const auto* immediate = std::get_if<common::RawValue>(&operand)) {
    if (const auto value = immediate->as_b32(); value) {
      return *value;
    } else {
      return std::unexpected(LaneFaultCause{value.error()});
    }
  }
  const auto value = registers.read(std::get<common::RegisterSlot>(operand));
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  if (const auto b32 = value->as_b32(); b32) {
    return *b32;
  } else {
    return std::unexpected(LaneFaultCause{b32.error()});
  }
}

auto b32_destination(const memory::RegisterView& registers,
                     common::RegisterSlot destination)
    -> std::expected<void, LaneFaultCause> {
  const auto width = registers.declared_width(destination);
  if (!width) {
    return std::unexpected(LaneFaultCause{width.error()});
  }
  if (*width != common::RawWidth::b32) {
    return std::unexpected(
        LaneFaultCause{common::RawValueError{common::RawWidth::b32, *width}});
  }
  return {};
}

/** @brief Serialize one u32 into the four PTX little-endian memory bytes. */
auto b32_bytes(std::uint32_t value) -> std::array<std::byte, 4> {
  return {std::byte{static_cast<std::uint8_t>(value)},
          std::byte{static_cast<std::uint8_t>(value >> 8U)},
          std::byte{static_cast<std::uint8_t>(value >> 16U)},
          std::byte{static_cast<std::uint8_t>(value >> 24U)}};
}

/** @brief Reconstruct one u32 from four PTX little-endian memory bytes. */
auto bytes_b32(const std::array<std::byte, 4>& value) -> std::uint32_t {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[0])) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[1]))
          << 8U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[2]))
          << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(value[3]))
          << 24U);
}

auto prepare_operation(const memory::RegisterView& registers,
                       const arith::context&, const exec_ir::Move& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto value =
      b32_operand(registers, exec_ir::B32Operand{operation.source});
  if (!value) {
    return std::unexpected(value.error());
  }
  if (const auto destination =
          b32_destination(registers, operation.destination);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .write = PreparedWrite{registers, operation.destination,
                             common::RawValue::b32(*value)},
      .memory_write = std::nullopt,
      .control = successor,
  };
}

auto prepare_operation(const memory::RegisterView& registers,
                       const arith::context& arithmetic,
                       const exec_ir::Add& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto lhs = b32_operand(registers, operation.lhs);
  if (!lhs) {
    return std::unexpected(lhs.error());
  }
  const auto rhs = b32_operand(registers, operation.rhs);
  if (!rhs) {
    return std::unexpected(rhs.error());
  }
  if (const auto destination =
          b32_destination(registers, operation.destination);
      !destination) {
    return std::unexpected(destination.error());
  }
  const auto sum = arith::add(arithmetic, *lhs, *rhs,
                              {.overflow = arith::integer_overflow_mode::wrap});
  if (!sum) {
    return std::unexpected(LaneFaultCause{sum.error()});
  }
  return PreparedEffect{
      .write = PreparedWrite{registers, operation.destination,
                             common::RawValue::b32(sum->value)},
      .memory_write = std::nullopt,
      .control = successor,
  };
}

/**
 * @brief Read one scalar memory value and stage its b32 register writeback.
 *
 * No register mutation occurs until the common commit phase.
 */
auto prepare_operation(LaneResourceResolver& resolver,
                       const exec_ir::Load& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto memory =
      resolver.resolve_memory(operation.space, operation.address);
  if (!memory) {
    return std::unexpected(memory.error());
  }
  std::array<std::byte, 4> bytes;
  if (const auto read = memory->first.read(memory->second, bytes, 4); !read) {
    return std::unexpected(LaneFaultCause{read.error()});
  }
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  if (const auto destination =
          b32_destination(registers->get(), operation.destination);
      !destination) {
    return std::unexpected(destination.error());
  }
  return PreparedEffect{
      .write = PreparedWrite{registers->get(), operation.destination,
                             common::RawValue::b32(bytes_b32(bytes))},
      .memory_write = std::nullopt,
      .control = successor,
  };
}

/**
 * @brief Validate one scalar memory write and stage its serialized bytes.
 *
 * The validation has no side effects, leaving the actual write for commit.
 */
auto prepare_operation(LaneResourceResolver& resolver,
                       const exec_ir::Store& operation,
                       common::ProgramCounter successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto memory =
      resolver.resolve_memory(operation.space, operation.address);
  if (!memory) {
    return std::unexpected(memory.error());
  }
  const auto registers = resolver.resolve();
  if (!registers) {
    return std::unexpected(registers.error());
  }
  const auto source = registers->get().read(operation.source);
  if (!source) {
    return std::unexpected(LaneFaultCause{source.error()});
  }
  const auto value = source->as_b32();
  if (!value) {
    return std::unexpected(LaneFaultCause{value.error()});
  }
  if (const auto valid = memory->first.validate_write(memory->second, 4, 4);
      !valid) {
    return std::unexpected(LaneFaultCause{valid.error()});
  }
  return PreparedEffect{
      .write = std::nullopt,
      .memory_write =
          PreparedMemoryWrite{memory->first, memory->second, b32_bytes(*value)},
      .control = successor,
  };
}

auto prepare_operation(const exec_ir::Branch& operation)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return PreparedEffect{
      .write = std::nullopt,
      .memory_write = std::nullopt,
      .control = operation.target,
  };
}

auto prepare_operation(const exec_ir::Exit&)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return PreparedEffect{
      .write = std::nullopt,
      .memory_write = std::nullopt,
      .control = ExitControl{},
  };
}

using LanePrepareHandler = std::expected<PreparedEffect, LaneFaultCause> (*)(
    LaneResourceResolver&, const arith::context&, const exec_ir::Operation&,
    std::optional<common::ProgramCounter>);

auto prepare_move_b32(LaneResourceResolver& registers,
                      const arith::context& arithmetic,
                      const exec_ir::Operation& operation,
                      std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = registers.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_operation(view->get(), arithmetic,
                           std::get<exec_ir::Move>(operation), *successor);
}

auto prepare_add_u32(LaneResourceResolver& registers,
                     const arith::context& arithmetic,
                     const exec_ir::Operation& operation,
                     std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  const auto view = registers.resolve();
  if (!view) {
    return std::unexpected(view.error());
  }
  return prepare_operation(view->get(), arithmetic,
                           std::get<exec_ir::Add>(operation), *successor);
}

/** @brief Adapt the u32 load record to the common opcode dispatch signature. */
auto prepare_load_u32(LaneResourceResolver& registers, const arith::context&,
                      const exec_ir::Operation& operation,
                      std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(registers, std::get<exec_ir::Load>(operation),
                           *successor);
}

/** @brief Adapt the u32 store record to the common opcode dispatch signature. */
auto prepare_store_u32(LaneResourceResolver& registers, const arith::context&,
                       const exec_ir::Operation& operation,
                       std::optional<common::ProgramCounter> successor)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(registers, std::get<exec_ir::Store>(operation),
                           *successor);
}

auto prepare_branch(LaneResourceResolver&, const arith::context&,
                    const exec_ir::Operation& operation,
                    std::optional<common::ProgramCounter>)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(std::get<exec_ir::Branch>(operation));
}

auto prepare_exit(LaneResourceResolver&, const arith::context&,
                  const exec_ir::Operation& operation,
                  std::optional<common::ProgramCounter>)
    -> std::expected<PreparedEffect, LaneFaultCause> {
  return prepare_operation(std::get<exec_ir::Exit>(operation));
}

using OpFamilySelector = std::expected<LanePrepareHandler, StepErrorCode> (*)(
    const exec_ir::Operation&);

auto unsupported_instruction() -> std::unexpected<StepErrorCode> {
  return std::unexpected(StepErrorCode::unsupported_instruction);
}

auto select_move(const exec_ir::Operation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  switch (std::get<exec_ir::Move>(operation).type) {
    case exec_ir::DataType::b32:
      return prepare_move_b32;
    case exec_ir::DataType::u32:
      return unsupported_instruction();
  }
  return unsupported_instruction();
}

auto select_add(const exec_ir::Operation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  switch (std::get<exec_ir::Add>(operation).type) {
    case exec_ir::DataType::u32:
      return prepare_add_u32;
    case exec_ir::DataType::b32:
      return unsupported_instruction();
  }
  return unsupported_instruction();
}

/** @brief Select only validated scalar-load address spaces and u32 handling. */
auto select_load(const exec_ir::Operation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  const auto& load = std::get<exec_ir::Load>(operation);
  switch (load.space) {
    case exec_ir::AddressSpace::generic:
    case exec_ir::AddressSpace::global:
      break;
    default:
      return unsupported_instruction();
  }
  switch (load.type) {
    case exec_ir::DataType::u32:
      return prepare_load_u32;
    case exec_ir::DataType::b32:
      return unsupported_instruction();
  }
  return unsupported_instruction();
}

/** @brief Select only validated scalar-store address spaces and u32 handling. */
auto select_store(const exec_ir::Operation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  const auto& store = std::get<exec_ir::Store>(operation);
  switch (store.space) {
    case exec_ir::AddressSpace::generic:
    case exec_ir::AddressSpace::global:
      break;
    default:
      return unsupported_instruction();
  }
  switch (store.type) {
    case exec_ir::DataType::u32:
      return prepare_store_u32;
    case exec_ir::DataType::b32:
      return unsupported_instruction();
  }
  return unsupported_instruction();
}

auto select_branch(const exec_ir::Operation&)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  return prepare_branch;
}

auto select_exit(const exec_ir::Operation&)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  return prepare_exit;
}

auto select_preparer(const exec_ir::Operation& operation)
    -> std::expected<LanePrepareHandler, StepErrorCode> {
  static_assert(std::variant_size_v<exec_ir::Operation> ==
                static_cast<std::size_t>(exec_ir::Op::exit) + 1);
  static_assert(static_cast<std::size_t>(exec_ir::Op::mov) == 0);
  static_assert(static_cast<std::size_t>(exec_ir::Op::add) == 1);
  static_assert(static_cast<std::size_t>(exec_ir::Op::ld) == 2);
  static_assert(static_cast<std::size_t>(exec_ir::Op::st) == 3);
  static_assert(static_cast<std::size_t>(exec_ir::Op::bra) == 4);
  static_assert(static_cast<std::size_t>(exec_ir::Op::exit) == 5);
  static constexpr std::array<OpFamilySelector,
                              std::variant_size_v<exec_ir::Operation>>
      dispatch{
          select_move,  select_add,    select_load,
          select_store, select_branch, select_exit,
      };
  return dispatch[static_cast<std::size_t>(exec_ir::op(operation))](operation);
}

struct ControlCommitter final {
  execution_model::Thread& thread;

  void operator()(common::ProgramCounter pc) const { thread.set_pc(pc); }

  void operator()(ExitControl) const { thread.mark_exited(); }
};

void apply_control(execution_model::Thread& thread,
                   const PreparedControl& control) {
  std::visit(ControlCommitter{thread}, control);
}

}  // namespace

InstExecuteEngine::InstExecuteEngine(runtime::LaunchRuntime& runtime,
                                     common::FunctionId function,
                                     const arith::context& arithmetic) noexcept
    : runtime_(runtime), function_(function), arithmetic_(arithmetic) {}

auto InstExecuteEngine::execute(execution_model::Warp& warp,
                                const execution_model::WarpIssueGroup& issue,
                                const exec_ir::Instruction& instruction,
                                std::optional<common::ProgramCounter> successor)
    -> std::expected<StepReport, StepError> {
  const auto* runtime_warp = runtime_.grid().find_warp(warp.id());
  if (runtime_warp != &warp) {
    return step_error(StepErrorCode::foreign_warp);
  }
  if (issue.lanes.size() != warp.architectural_warp_size()) {
    return step_error(StepErrorCode::lane_mask_width);
  }
  if (issue.empty()) {
    return step_error(StepErrorCode::empty_issue);
  }

  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) {
      continue;
    }
    if (!warp.valid_mask().test(lane)) {
      return step_error(StepErrorCode::invalid_lane, lane);
    }
    const auto& thread = warp.thread(lane);
    if (!thread.ready()) {
      return step_error(StepErrorCode::lane_not_ready, lane);
    }
    if (thread.pc() != issue.pc) {
      return step_error(StepErrorCode::pc_mismatch, lane);
    }
  }

  if (exec_ir::may_fallthrough(instruction) && !successor) {
    return step_error(StepErrorCode::missing_fallthrough);
  }

  const auto prepare = select_preparer(instruction.operation);
  if (!prepare) {
    return step_error(prepare.error());
  }

  StepReport report;
  std::vector<PreparedLane> prepared;
  prepared.reserve(issue.size());

  for (std::uint32_t index = 0; index < warp.architectural_warp_size();
       ++index) {
    const execution_model::LaneId lane{index};
    if (!issue.lanes.test(lane)) {
      continue;
    }
    auto& thread = warp.thread(lane);
    LaneResourceResolver registers(runtime_, thread, function_);
    if (instruction.predicate) {
      const auto view = registers.resolve();
      if (!view) {
        report.faults.push_back({lane, view.error()});
        continue;
      }
      const auto predicate = view->get().read(instruction.predicate->source);
      if (!predicate) {
        report.faults.push_back({lane, predicate.error()});
        continue;
      }
      const auto value = predicate->as_pred();
      if (!value) {
        report.faults.push_back({lane, value.error()});
        continue;
      }
      if (*value == instruction.predicate->negated) {
        prepared.push_back({&thread,
                            {.write = std::nullopt,
                             .memory_write = std::nullopt,
                             .control = *successor}});
        continue;
      }
    }
    const auto effect =
        (*prepare)(registers, arithmetic_, instruction.operation, successor);
    if (!effect) {
      report.faults.push_back({lane, effect.error()});
      continue;
    }
    prepared.push_back({&thread, *effect});
  }

  for (auto& lane : prepared) {
    if (lane.effect.write) {
      if (const auto write = lane.effect.write->registers.write(
              lane.effect.write->destination, lane.effect.write->value);
          !write) {
        report.faults.push_back({lane.thread->lane_id(), write.error()});
        continue;
      }
    }
    if (lane.effect.memory_write) {
      auto& write = *lane.effect.memory_write;
      if (const auto result = write.space.write(write.address, write.value, 4);
          !result) {
        report.faults.push_back({lane.thread->lane_id(), result.error()});
        continue;
      }
    }
    apply_control(*lane.thread, lane.effect.control);
  }

  for (const auto& fault : report.faults) {
    warp.thread(fault.lane).mark_trapped();
  }
  return report;
}

}  // namespace ptxsim::inst_execute_engine
