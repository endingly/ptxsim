#include <ptxsim/exec_ir/exec_ir.hpp>

#include <algorithm>
#include <limits>
#include <utility>

#include <ptxsim/exec_ir/instruction_catalogue.gen.hpp>

namespace ptxsim::exec_ir {


namespace {

/** @brief Build one typed structural failure without accessing execution state. */
[[nodiscard]] auto error(
    ProgramErrorCode code,
    std::optional<common::FunctionId> function = std::nullopt,
    std::optional<common::ProgramCounter> pc = std::nullopt,
    std::optional<common::RawWidth> actual = std::nullopt,
    std::optional<StorageId> storage = std::nullopt)
    -> std::unexpected<ProgramError> {
  return std::unexpected(ProgramError{code, function, pc, actual, storage});
}

/** @brief Check an actual physical register storage width. */
[[nodiscard]] auto valid_width(common::RawWidth width) noexcept -> bool {
  switch (width) {
    case common::RawWidth::pred:
    case common::RawWidth::b8:
    case common::RawWidth::b16:
    case common::RawWidth::b32:
    case common::RawWidth::b64:
    case common::RawWidth::b128:
      return true;
  }
  return false;
}

/** @brief Test a known byte alignment without imposing one on opaque declarations. */
[[nodiscard]] auto valid_alignment(std::uint64_t alignment) noexcept -> bool {
  return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

/** @brief Recognize every owned parameter declaration role before interpreting ABI metadata. */
[[nodiscard]] auto valid_parameter_role(ParameterDeclarationRole role) noexcept -> bool {
  switch(role) {
    case ParameterDeclarationRole::EntryInput:
    case ParameterDeclarationRole::DeviceInput:
    case ParameterDeclarationRole::DeviceReturn:
    case ParameterDeclarationRole::BodyLocal:
    case ParameterDeclarationRole::LegacyEntryInput: return true;
  }
  return false;
}
/** @brief Recognize the two declaration ownership roles of an opaque resource. */
[[nodiscard]] auto valid_resource_role(ResourceDeclarationRole role) noexcept -> bool {
  switch(role) {
    case ResourceDeclarationRole::Module:
    case ResourceDeclarationRole::EntryInput: return true;
  }
  return false;
}
/** @brief Recognize the complete native opaque resource kind domain. */
[[nodiscard]] auto valid_opaque_kind(OpaqueResourceKind kind) noexcept -> bool {
  switch(kind) {
    case OpaqueResourceKind::Texture:
    case OpaqueResourceKind::Sampler:
    case OpaqueResourceKind::Surface: return true;
  }
  return false;
}

/** @brief Check typed scalar-versus-opaque element membership. */
[[nodiscard]] auto valid_element_type(
    const std::variant<DataType, OpaqueResourceKind>& element) noexcept -> bool {
  if (const auto* scalar = std::get_if<DataType>(&element))
    return is_valid(*scalar);
  return valid_opaque_kind(std::get<OpaqueResourceKind>(element));
}

/** @brief Check the explicit derived entry packing without rechecking PTX legality. */
[[nodiscard]] auto valid_entry_layout(const FunctionLayout& layout,
                                      const ProgramDefinition& definition,
                                      const FunctionDeclaration& declaration)
    noexcept -> bool {
  if (!layout.entry_packing_available)
    return layout.entry_parameters.empty() && layout.entry_parameter_size == 0;
  if (!declaration.opaque_entry_parameters.empty()) return false;
  std::size_t end = 0;
  std::size_t slot_index = 0;
  for (const auto id : declaration.parameters) {
    if (id.value() >= definition.parameters.size()) return false;
    const auto& parameter = definition.parameters[id.value()];
    if (parameter.owner_declaration != declaration.id || parameter.owner_function != layout.id)
      return false;
    if (parameter.role != ParameterDeclarationRole::EntryInput &&
        parameter.role != ParameterDeclarationRole::LegacyEntryInput) continue;
    if (slot_index >= layout.entry_parameters.size()) return false;
    const auto& slot = layout.entry_parameters[slot_index++];
    if (!valid_alignment(slot.alignment) || slot.size == 0 ||
        slot.parameter != id || !parameter.byte_extent ||
        slot.size != *parameter.byte_extent || slot.alignment != parameter.alignment)
      return false;
    const auto remainder = end % slot.alignment;
    const auto padding = remainder == 0 ? 0 : slot.alignment - remainder;
    if (padding > std::numeric_limits<std::size_t>::max() - end)
      return false;
    const auto offset = end + padding;
    if (slot.offset != offset ||
        slot.size > std::numeric_limits<std::size_t>::max() - offset)
      return false;
    end = offset + slot.size;
  }
  return slot_index == layout.entry_parameters.size() && end == layout.entry_parameter_size;
}

/** @brief Validate only owned graph/container shape, never execution capability. */
[[nodiscard]] auto validate(const ProgramDefinition& definition)
    -> std::expected<void, ProgramError> {
  constexpr auto max_id_count = std::numeric_limits<std::uint32_t>::max();
  if (definition.functions.size() > max_id_count ||
      definition.function_declarations.size() > max_id_count)
    return error(ProgramErrorCode::function_count_not_representable);

  // Closed metadata domains must be established before deriving role-dependent views.
  for (const auto& parameter : definition.parameters)
    if (!valid_parameter_role(parameter.role))
      return error(ProgramErrorCode::invalid_parameter_declaration, parameter.owner_function);
  for (const auto& resource : definition.resources)
    if (!valid_resource_role(resource.role) || !valid_opaque_kind(resource.kind))
      return error(ProgramErrorCode::invalid_resource_declaration, resource.entry_owner);

  std::vector<bool> declaration_seen(definition.function_declarations.size(), false);
  std::vector<const FunctionLayout*> bodies;
  for (std::size_t index = 0; index < definition.functions.size(); ++index) {
    const auto& function = definition.functions[index];
    if (function.id.value() != index)
      return error(ProgramErrorCode::function_id_not_dense, function.id);
    if (function.body.has_value() != function.definition.has_value())
      return error(ProgramErrorCode::invalid_layout, function.id);
    for (const auto id : function.declarations) {
      if (id.value() >= definition.function_declarations.size() ||
          declaration_seen[id.value()] ||
          definition.function_declarations[id.value()].function != function.id)
        return error(ProgramErrorCode::invalid_declaration_reference, function.id);
      declaration_seen[id.value()] = true;
    }
    if (!function.body)
      continue;
    if (function.definition->value() >= definition.function_declarations.size())
      return error(ProgramErrorCode::invalid_declaration_reference, function.id);
    const auto& declaration = definition.function_declarations[function.definition->value()];
    if (declaration.function != function.id || declaration.is_prototype ||
        declaration.is_entry != function.is_entry)
      return error(ProgramErrorCode::invalid_declaration_reference, function.id);
    const auto& body = *function.body;
    if (body.id != function.id)
      return error(ProgramErrorCode::invalid_layout, function.id);
    if (body.begin > definition.instructions.size() ||
        body.instruction_count > definition.instructions.size() - body.begin)
      return error(ProgramErrorCode::invalid_layout_range, function.id);
    for (const auto width : body.register_widths)
      if (!valid_width(width))
        return error(ProgramErrorCode::invalid_register_width, function.id,
                     std::nullopt, width);
    if (!valid_entry_layout(body, definition, declaration))
      return error(ProgramErrorCode::invalid_entry_parameter_layout, function.id);
    bodies.push_back(&body);
  }
  if (std::find(declaration_seen.begin(), declaration_seen.end(), false) != declaration_seen.end())
    return error(ProgramErrorCode::invalid_declaration_reference);
  std::sort(bodies.begin(), bodies.end(), [](const auto* left, const auto* right) {
    return left->begin < right->begin ||
           (left->begin == right->begin && left->instruction_count < right->instruction_count);
  });
  std::size_t end = 0;
  for (const auto* body : bodies) {
    if (body->begin != end)
      return error(ProgramErrorCode::invalid_layout, body->id);
    end += body->instruction_count;
  }
  if (end != definition.instructions.size())
    return error(ProgramErrorCode::invalid_layout);

  for (std::size_t index = 0; index < definition.function_declarations.size(); ++index) {
    const auto& declaration = definition.function_declarations[index];
    if (declaration.id.value() != index)
      return error(ProgramErrorCode::declaration_id_not_dense, declaration.function);
    if (declaration.function.value() >= definition.functions.size() ||
        (declaration.source_profile && *declaration.source_profile >= definition.source_profiles.size()))
      return error(ProgramErrorCode::invalid_declaration_reference, declaration.function);
    for (const auto id : declaration.parameters)
      if (id.value() >= definition.parameters.size() ||
          definition.parameters[id.value()].owner_declaration != declaration.id)
        return error(ProgramErrorCode::invalid_parameter_declaration, declaration.function);
    for (const auto id : declaration.opaque_entry_parameters)
      if (id.value() >= definition.resources.size() ||
          definition.resources[id.value()].entry_declaration != declaration.id)
        return error(ProgramErrorCode::invalid_resource_declaration, declaration.function);
  }
  for (std::size_t index = 0; index < definition.parameters.size(); ++index) {
    const auto& parameter = definition.parameters[index];
    if (parameter.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense, parameter.owner_function);
    if (parameter.owner_declaration.value() >= definition.function_declarations.size() ||
        parameter.owner_function.value() >= definition.functions.size() ||
        definition.function_declarations[parameter.owner_declaration.value()].function != parameter.owner_function ||
        !is_valid(parameter.scalar_type))
      return error(ProgramErrorCode::invalid_parameter_declaration, parameter.owner_function);
    const auto& owner = definition.function_declarations[parameter.owner_declaration.value()];
    if (std::count(owner.parameters.begin(), owner.parameters.end(), parameter.id) != 1)
      return error(ProgramErrorCode::invalid_parameter_declaration, parameter.owner_function);
  }
  for (std::size_t index = 0; index < definition.storage_declarations.size(); ++index) {
    const auto& declaration = definition.storage_declarations[index];
    if (declaration.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense, std::nullopt,
                   std::nullopt, std::nullopt, declaration.id);
    if (static_cast<unsigned>(declaration.space) > static_cast<unsigned>(StorageSpace::local) ||
        static_cast<unsigned>(declaration.declaration_kind) > static_cast<unsigned>(StorageDeclarationKind::External) ||
        static_cast<unsigned>(declaration.initialization) > static_cast<unsigned>(StorageInitialization::opaque_static) ||
        !valid_element_type(declaration.element_type) ||
        (declaration.owner_function && declaration.owner_function->value() >= definition.functions.size()) ||
        (declaration.alignment && !valid_alignment(*declaration.alignment)) ||
        (declaration.explicit_alignment && !valid_alignment(*declaration.explicit_alignment)))
      return error(ProgramErrorCode::invalid_storage_declaration, declaration.owner_function,
                   std::nullopt, std::nullopt, declaration.id);
    for (const auto& element : declaration.initializer) {
      const auto* relocation = std::get_if<StorageRelocation>(&element.value);
      if (!relocation)
        continue;
      if (const auto* target = std::get_if<StorageId>(&relocation->target)) {
        if (target->value() >= definition.storage_declarations.size())
          return error(ProgramErrorCode::invalid_storage_declaration, declaration.owner_function,
                       std::nullopt, std::nullopt, declaration.id);
      } else if (const auto* target = std::get_if<ResourceId>(&relocation->target)) {
        if (target->value() >= definition.resources.size())
          return error(ProgramErrorCode::invalid_storage_declaration, declaration.owner_function,
                       std::nullopt, std::nullopt, declaration.id);
      } else if (std::get<common::FunctionId>(relocation->target).value() >= definition.functions.size()) {
        return error(ProgramErrorCode::invalid_storage_declaration, declaration.owner_function,
                     std::nullopt, std::nullopt, declaration.id);
      }
    }
  }
  for (std::size_t index = 0; index < definition.resources.size(); ++index) {
    const auto& resource = definition.resources[index];
    if (resource.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense);
    if ((resource.entry_owner && resource.entry_owner->value() >= definition.functions.size()) ||
        (resource.entry_declaration && resource.entry_declaration->value() >= definition.function_declarations.size()) ||
        (resource.storage && resource.storage->value() >= definition.storage_declarations.size()))
      return error(ProgramErrorCode::invalid_resource_declaration, resource.entry_owner);
    if (resource.role == ResourceDeclarationRole::EntryInput) {
      if (!resource.entry_owner || !resource.entry_declaration || resource.storage ||
          !definition.functions[resource.entry_owner->value()].is_entry ||
          definition.function_declarations[resource.entry_declaration->value()].function != *resource.entry_owner)
        return error(ProgramErrorCode::invalid_resource_declaration, resource.entry_owner);
      const auto& owner = definition.function_declarations[resource.entry_declaration->value()];
      if (std::count(owner.opaque_entry_parameters.begin(), owner.opaque_entry_parameters.end(), resource.id) != 1)
        return error(ProgramErrorCode::invalid_resource_declaration, resource.entry_owner);
    } else if (resource.entry_owner || resource.entry_declaration || !resource.storage) {
      return error(ProgramErrorCode::invalid_resource_declaration, resource.entry_owner);
    }
  }
  for (std::size_t index = 0; index < definition.prototypes.size(); ++index) {
    const auto& prototype = definition.prototypes[index];
    if (prototype.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense);
    if (prototype.owner_declaration.value() >= definition.function_declarations.size())
      return error(ProgramErrorCode::invalid_callable_metadata);
  }
  for (std::size_t index = 0; index < definition.call_target_sets.size(); ++index) {
    const auto& targets = definition.call_target_sets[index];
    if (targets.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense);
    if (targets.owner_declaration.value() >= definition.function_declarations.size())
      return error(ProgramErrorCode::invalid_callable_metadata);
    for (const auto target : targets.targets)
      if (target.value() >= definition.functions.size())
        return error(ProgramErrorCode::invalid_callable_metadata, target);
  }
  for (std::size_t index = 0; index < definition.branch_target_sets.size(); ++index) {
    const auto& targets = definition.branch_target_sets[index];
    if (targets.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense);
    if (targets.owner_function.value() >= definition.functions.size() ||
        !definition.functions[targets.owner_function.value()].body)
      return error(ProgramErrorCode::invalid_callable_metadata, targets.owner_function);
  }
  for (std::size_t index = 0; index < definition.call_tables.size(); ++index) {
    const auto& table = definition.call_tables[index];
    if (table.id.value() != index)
      return error(ProgramErrorCode::metadata_id_not_dense);
    if (table.storage.value() >= definition.storage_declarations.size() ||
        (table.owner_function && table.owner_function->value() >= definition.functions.size()) ||
        !is_valid(table.element_type))
      return error(ProgramErrorCode::invalid_callable_metadata, table.owner_function);
    for (const auto& slot : table.slots)
      if (slot.function.value() >= definition.functions.size())
        return error(ProgramErrorCode::invalid_callable_metadata, slot.function);
  }
  for (const auto& alias : definition.aliases)
    if (alias.function.value() >= definition.functions.size())
      return error(ProgramErrorCode::invalid_callable_metadata, alias.function);
  for (const auto& instruction : definition.instructions) {
    if (!instruction)
      return error(ProgramErrorCode::null_instruction);
    if (!structurally_valid(*instruction))
      return error(ProgramErrorCode::invalid_instruction_shape);
  }
  return {};
}

}  // namespace

/** @brief Actual const allocation holding fresh independently copied value graphs. */
struct ExecutableProgram::Storage {
  /** @brief Construct a fresh graph; only actual const final objects are allocated. */
  explicit Storage(const ProgramDefinition& source)
      : functions(source.functions), declarations(source.function_declarations),
        storage_declarations(source.storage_declarations), resources(source.resources),
        parameters(source.parameters), prototypes(source.prototypes),
        call_target_sets(source.call_target_sets), branch_target_sets(source.branch_target_sets),
        call_tables(source.call_tables), aliases(source.aliases), profiles(source.source_profiles) {
    instructions.reserve(source.instructions.size());
    for (const auto& instruction : source.instructions)
      instructions.push_back(freeze_instruction(*instruction));
  }
  /** @brief Actual const final objects with independently copied nested containers. */
  std::vector<std::unique_ptr<const Instruction>> instructions;
  /** @brief Fresh canonical callable records. */
  std::vector<FunctionRecord> functions;
  /** @brief Fresh specific declaration metadata. */
  std::vector<FunctionDeclaration> declarations;
  /** @brief Fresh full storage declarations. */
  std::vector<StorageDeclaration> storage_declarations;
  /** @brief Fresh full opaque resource declarations. */
  std::vector<ResourceDeclaration> resources;
  /** @brief Fresh full scalar parameter declarations. */
  std::vector<ParameterDeclaration> parameters;
  /** @brief Fresh indirect callable prototype metadata. */
  std::vector<PrototypeRecord> prototypes;
  /** @brief Fresh callable target-set vectors. */
  std::vector<CallTargetSetRecord> call_target_sets;
  /** @brief Fresh local branch target-set vectors. */
  std::vector<BranchTargetSetRecord> branch_target_sets;
  /** @brief Fresh sparse callable storage metadata. */
  std::vector<CallTableRecord> call_tables;
  /** @brief Fresh owned alias names and targets. */
  std::vector<FunctionAlias> aliases;
  /** @brief Fresh source interpretation profile values. */
  std::vector<SourceProfile> profiles;
};

ExecutableProgram::ExecutableProgram(std::shared_ptr<const Storage> storage) noexcept
    : storage_(std::move(storage)) {}

auto ExecutableProgram::create(const ProgramDefinition& definition)
    -> std::expected<ExecutableProgram, ProgramError> {
  if (const auto checked = validate(definition); !checked)
    return std::unexpected(checked.error());
  return ExecutableProgram{std::make_shared<const Storage>(definition)};
}

auto ExecutableProgram::function_record(common::FunctionId id) const
    -> std::expected<std::reference_wrapper<const FunctionRecord>, ProgramError> {
  if (!storage_)
    return error(ProgramErrorCode::empty_program_handle, id);
  if (id.value() >= storage_->functions.size())
    return error(ProgramErrorCode::function_not_found, id);
  return std::cref(storage_->functions[id.value()]);
}

auto ExecutableProgram::function_layout(common::FunctionId id) const
    -> std::expected<std::reference_wrapper<const FunctionLayout>, ProgramError> {
  const auto record = function_record(id);
  if (!record)
    return std::unexpected(record.error());
  if (!record->get().body)
    return error(ProgramErrorCode::nonexecutable_function, id);
  return std::cref(*record->get().body);
}

auto ExecutableProgram::flat_offset(common::CodeLocation location) const
    -> std::expected<std::size_t, ProgramError> {
  const auto layout = function_layout(location.function);
  if (!layout)
    return std::unexpected(layout.error());
  if (location.pc.value() >= layout->get().instruction_count)
    return error(ProgramErrorCode::pc_out_of_range, location.function, location.pc);
  return layout->get().begin + location.pc.value();
}

auto ExecutableProgram::fetch(common::CodeLocation location) const
    -> std::expected<std::reference_wrapper<const Instruction>, ProgramError> {
  const auto index = flat_offset(location);
  if (!index)
    return std::unexpected(index.error());
  return std::cref(*storage_->instructions[*index]);
}

auto ExecutableProgram::fallthrough(common::CodeLocation location) const
    -> std::expected<common::CodeLocation, ProgramError> {
  const auto layout = function_layout(location.function);
  if (!layout)
    return std::unexpected(layout.error());
  if (location.pc.value() >= layout->get().instruction_count)
    return error(ProgramErrorCode::pc_out_of_range, location.function, location.pc);
  if (location.pc.value() == std::numeric_limits<std::uint32_t>::max() ||
      location.pc.value() + 1 >= layout->get().instruction_count)
    return error(ProgramErrorCode::no_fallthrough, location.function, location.pc);
  return common::CodeLocation{location.function, common::ProgramCounter{location.pc.value() + 1}};
}

auto ExecutableProgram::functions() const noexcept -> std::span<const FunctionRecord> {
  return storage_ ? std::span<const FunctionRecord>{storage_->functions} : std::span<const FunctionRecord>{};
}
auto ExecutableProgram::function_declarations() const noexcept -> std::span<const FunctionDeclaration> {
  return storage_ ? std::span<const FunctionDeclaration>{storage_->declarations} : std::span<const FunctionDeclaration>{};
}
auto ExecutableProgram::storage_declarations() const noexcept -> std::span<const StorageDeclaration> {
  return storage_ ? std::span<const StorageDeclaration>{storage_->storage_declarations} : std::span<const StorageDeclaration>{};
}
auto ExecutableProgram::resources() const noexcept -> std::span<const ResourceDeclaration> {
  return storage_ ? std::span<const ResourceDeclaration>{storage_->resources} : std::span<const ResourceDeclaration>{};
}
auto ExecutableProgram::parameters() const noexcept -> std::span<const ParameterDeclaration> {
  return storage_ ? std::span<const ParameterDeclaration>{storage_->parameters} : std::span<const ParameterDeclaration>{};
}
auto ExecutableProgram::prototypes() const noexcept -> std::span<const PrototypeRecord> {
  return storage_ ? std::span<const PrototypeRecord>{storage_->prototypes} : std::span<const PrototypeRecord>{};
}
auto ExecutableProgram::call_target_sets() const noexcept -> std::span<const CallTargetSetRecord> {
  return storage_ ? std::span<const CallTargetSetRecord>{storage_->call_target_sets} : std::span<const CallTargetSetRecord>{};
}
auto ExecutableProgram::branch_target_sets() const noexcept -> std::span<const BranchTargetSetRecord> {
  return storage_ ? std::span<const BranchTargetSetRecord>{storage_->branch_target_sets} : std::span<const BranchTargetSetRecord>{};
}
auto ExecutableProgram::call_tables() const noexcept -> std::span<const CallTableRecord> {
  return storage_ ? std::span<const CallTableRecord>{storage_->call_tables} : std::span<const CallTableRecord>{};
}
auto ExecutableProgram::aliases() const noexcept -> std::span<const FunctionAlias> {
  return storage_ ? std::span<const FunctionAlias>{storage_->aliases} : std::span<const FunctionAlias>{};
}
auto ExecutableProgram::source_profiles() const noexcept -> std::span<const SourceProfile> {
  return storage_ ? std::span<const SourceProfile>{storage_->profiles} : std::span<const SourceProfile>{};
}

auto to_string(const ExecutableProgram& program) -> std::string {
  std::string output;
  for (const auto& function : program.functions()) {
    output += common::to_string(function.id);
    output += function.body ? ":\n" : ": <declaration>\n";
    if (!function.body)
      continue;
    for (std::uint32_t index = 0; index < function.body->instruction_count; ++index) {
      const common::CodeLocation location{function.id, common::ProgramCounter{index}};
      output += "  ";
      output += common::to_string(location.pc);
      output += ": ";
      output += to_string(program.fetch(location)->get());
      output += '\n';
    }
  }
  return output;
}

}  // namespace ptxsim::exec_ir
