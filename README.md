# Talk on LLVM and Clang tooling at C++Now 2017

TODO:

- Write:
  * libclang:
    1. ASTDumper again
    2. Code completion again
    3. Search tool to do regex searches but where can specify what kind of symbol (decl/type/identifier/parameter/declaration etc.)
    4. Include graphs
  * libtooling:
    1. Look at old tools,
    2. Virtual Destructor checker
    3. Using instead of Typedef
    4. enable_if_t instead of enable_if
    5. Include sorter
    6. Include graph / transitive includes finder (visualizer?)
    7. Search Tool
    8. Figure out plugins

- Read:
  * How to emit LLVM from code (try to use CodeGenAction)
  * How to walk control flow graph / basic blocks
  * Learn more about SCCIterators and stuff
  * Write basic pass

- Goal: const analysis



Ideas:

- Text Editor
- Has slide to increase/decrease number of features (3 features = console for astdump)
- Checkbox to enable/disable bugs
