# Notes

Time available: 30 minutes
Major topics to cover:

* LLVM in general (vs. GCC) (~5 minutes)
* Clang AST (~7.5 minutes)
* Clang Tooling (~7.5 minutes)
* Building a Tool (~10 minutes)

## LLVM in general

History of LLVM

Most compilers have a three phase design

* LLVM is not built monolithically like GCC.
* Rather, it is a set of libraries for various aspects of code parsing, optimization and transformation, that, when combined, give you a complete compiler.

Frontend:
* Begin with Lexer, which tokenizes a stream of characters for the parsing.
* The parser takes those tokens and transforms them into an AST.
* Does syntactic analysis (makes sure you're not adding a number to a brace).
* Then semantic analysis such as type checking and making sure you are following the rules of the C++ standard.

Optimizer:
* Language independent optimizer that takes code in intermediate representation.
* Performs language-independent optimizations such as loop unrolling or constant propagation. (Show LLVM IR)
* Built as a set of modular passes.

Backend:
* Target platform (ISA) specific code optimizations in assembly.
* Use the right instructions as provided by the hardware.
* Final machine code generation.

Todo:

* Setup code
* Make symlinks
* Open in llvm directory
* practice
