#pragma once

#include <cstddef>

namespace ptxsim::memory {

struct EntryParameterSpec {
  std::size_t size = 0;
};

struct FunctionParameterSpec {
  std::size_t size = 0;
};

}  // namespace ptxsim::memory
