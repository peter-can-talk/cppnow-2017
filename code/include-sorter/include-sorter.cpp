// Clang Includes
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Rewrite/Frontend/FixItRewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

// LLVM Includes
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

// Standard Includes
#include <memory>
#include <string>
#include <type_traits>

namespace IncludeSorter {

/// Represents an include in source code.
struct Include {
  Include(const std::string& Filename, bool Angled)
  : Filename(Filename), Angled(Angled) {}

  /// The name of the included file.
  std::string Filename;

  /// Wether the file was included with angle brackets.
  bool Angled;
};

namespace {

/// Takes a vector of includes and sorts them lexicographically, optionally in
/// reverse order.
std::string
sortIncludes(llvm::SmallVectorImpl<Include>& Includes, bool Reverse) {
  /// Sort the includes in-place first.
  std::sort(Includes.begin(), Includes.end(), [=](auto& first, auto& second) {
    return Reverse ? (first.Filename > second.Filename)
                   : (first.Filename < second.Filename);
  });

  /// Join the includes back together.
  std::string JoinedLines;
  JoinedLines.reserve(Includes.size() * 39);  /// 39 = estimate of average line

  for (auto Include = Includes.begin(); Include != Includes.end();) {
    const auto left = Include->Angled ? "<" : "\"";
    const auto right = Include->Angled ? ">" : "\"";
    JoinedLines +=
        (llvm::Twine("#include ") + left + Include->Filename + right).str();
    if (++Include != Includes.end()) JoinedLines += '\n';
  }

  return JoinedLines;
}
}  // namespace

/// Captures #include directives and sorts them after every block.
///
/// The algorithm proceeds by collecting all included files into a vector and
/// whenever the distance between two includes is more than one line, the files
/// picked up until then are sorted and the source code rewritten.
class PreprocessorCallback : public clang::PPCallbacks {
 public:
  /// Constructor.
  ///
  /// \param Rewriter The object to rewrite the source code
  /// \param Reverse Whether to sort includes in reverse.
  explicit PreprocessorCallback(clang::Rewriter& Rewriter, bool Reverse)
  : SourceManager(Rewriter.getSourceMgr())
  , Rewriter(Rewriter)
  , Reverse(Reverse) {}

  /// Collects the included file and possibly performs a sorting.
  void InclusionDirective(clang::SourceLocation HashLocation,
                          const clang::Token&,
                          llvm::StringRef Filename,
                          bool Angled,
                          clang::CharSourceRange Range,
                          const clang::FileEntry*,
                          llvm::StringRef,
                          llvm::StringRef,
                          const clang::Module*) override {
    if (!SourceManager.isInMainFile(HashLocation)) return;

    // Need to find the line number.
    const auto[FileID, Offset] = SourceManager.getDecomposedLoc(HashLocation);

    bool Invalid = false;
    const unsigned LineNumber =
        SourceManager.getLineNumber(FileID, Offset, &Invalid);
    assert(!Invalid && "Error retrieving line number");

    // Whenever the distance between lines is more than one, we have a "block",
    // so sort this block.
    if (!Includes.empty() && LineNumber > LastLineNumber + 1) {
      SortCurrent();
    }

    if (Includes.empty()) FirstLocation = HashLocation;
    Includes.emplace_back(Filename, Angled);
    LastLineNumber = LineNumber;
    LastLocation = Range.getEnd();
  }

  /// Sort the final chunk of lines.
  void EndOfMainFile() override {
    if (!Includes.empty()) SortCurrent();
  }

 private:
  /// Sorts the current includes, rewrites the source code and clears the state.
  void SortCurrent() {
    const std::string JoinedLines = sortIncludes(Includes, Reverse);
    clang::SourceRange Range(FirstLocation, LastLocation);
    Rewriter.ReplaceText(Range, JoinedLines);
    Includes.clear();
  }

  /// The current block of includes.
  llvm::SmallVector<Include, 16> Includes;

  /// The first location of the current block.
  clang::SourceLocation FirstLocation;

  /// The last location of the current block.
  clang::SourceLocation LastLocation;

  /// The line number of the last location of the current block.
  unsigned LastLineNumber;

  /// The `SourceManager` to rewrite text.
  const clang::SourceManager& SourceManager;

  /// The `Rewriter` to rewrite text.
  clang::Rewriter& Rewriter;

  /// Whether to sort in reverse.
  bool Reverse;
};

/// The action that registers the preprocessor callbacks.
///
/// Note that we can skip the consumer in this case.
class Action : public clang::PreprocessOnlyAction {
 public:
  /// Constructor.
  ///
  /// \param Reverse Whether to sort in reverse
  explicit Action(bool Reverse) : Reverse(Reverse) {}

  /// Called before any file is even touched. Allows us to register a rewriter.
  bool BeginInvocation(clang::CompilerInstance& Compiler) override {
    Rewriter.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    return true;
  }

  /// Adds our preprocessor callback to the compiler instance.
  bool BeginSourceFileAction(clang::CompilerInstance& Compiler,
                             llvm::StringRef Filename) override {
    auto hooks = std::make_unique<PreprocessorCallback>(Rewriter, Reverse);
    Compiler.getPreprocessor().addPPCallbacks(std::move(hooks));
    return true;
  }

  /// Writes the rewritten source code back out to disk.
  void EndSourceFileAction() override {
    const auto FileID = Rewriter.getSourceMgr().getMainFileID();
    Rewriter.getEditBuffer(FileID).write(llvm::outs());
  }

 private:
  /// The rewriter to rewrite source code. Forwarded to the callback.
  clang::Rewriter Rewriter;

  /// Whether to sort in reverse order. Forwarded to the callback.
  bool Reverse;
};
}  // namespace IncludeSorter

namespace {
llvm::cl::OptionCategory includeSorterCategory("minus-tool options");
llvm::cl::extrahelp includeSorterCategoryHelp(R"(
  Sorts your Includes alphabetically.
)");

llvm::cl::opt<bool> ReverseOption("reverse",
                                  llvm::cl::desc("Sort in reversed order"),
                                  llvm::cl::cat(includeSorterCategory));
llvm::cl::alias
    ReverseShortOption("r",
                       llvm::cl::desc("Alias for the -reverse option"),
                       llvm::cl::aliasopt(ReverseOption));
}  // namespace

/// A custom `FrontendActionFactory` so that we can pass the options
/// to the constructor of the tool.
struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new IncludeSorter::Action(ReverseOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, includeSorterCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(new ToolFactory());
}
