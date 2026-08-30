#include <resolved_ir.gen.hpp>

int main() {
  return ptx_frontend::resolved_ir::Mov::get_resolved_descriptor()
                 .variants.empty();
}
