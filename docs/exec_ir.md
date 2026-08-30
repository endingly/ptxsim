# Execution IR

## M2-00 status

The build-tree `ptxsim::exec_ir` CMake target exists and is
frontend-independent. It has no public instruction, operand, ID, or value
types yet, so M2-00 does not install or export it.

## Boundary

`exec_ir` will own typed execution facts after lowering. It will not retain
`ptx_frontend` objects, PTX modifier spellings, arithmetic backend types, or
SASS/timing details.

## Non-goals in M2-00

M2-00 does not define instruction records, operands, strong IDs, raw values,
or a verifier. Those types begin in M2-01.
