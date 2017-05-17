// Clang includes
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

// LLVM includes
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

// Standard includes
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>

namespace IncludeSorter {

struct Include {
  Include(const std::string& filename_, bool angled_)
  : filename(filename_), angled(angled_) {
  }

  std::string filename;
  bool angled;
};

namespace {
std::string
sortIncludes(llvm::SmallVectorImpl<Include>& includes, bool reverse) {
  std::sort(includes.begin(), includes.end(), [=](auto& first, auto& second) {
    return reverse ? (first.filename > second.filename)
                   : (first.filename < second.filename);
  });

  std::string joined;
  joined.reserve(includes.size() * 32);
  for (auto include = includes.begin(); include != includes.end();) {
    const auto left = include->angled ? "<" : "\"";
    const auto right = include->angled ? ">" : "\"";
    joined +=
        (llvm::Twine("#include ") + left + include->filename + right).str();
    if (++include != includes.end()) joined += '\n';
  }

  return joined;
}
}  // namespace

class PreprocessorCallback : public clang::PPCallbacks {
 public:
  explicit PreprocessorCallback(clang::Rewriter& rewriter, bool reverse)
  : _sourceManager(rewriter.getSourceMgr())
  , _rewriter(rewriter)
  , _reverse(reverse) {
  }

  void EndOfMainFile() override {
    _sortCurrent();
  }

  void InclusionDirective(clang::SourceLocation hashLocation,
                          const clang::Token&,
                          llvm::StringRef filename,
                          bool angled,
                          clang::CharSourceRange range,
                          const clang::FileEntry*,
                          llvm::StringRef,
                          llvm::StringRef,
                          const clang::Module*) override {
    if (!_sourceManager.isInMainFile(hashLocation)) return;
    const auto[fileID, offset] = _sourceManager.getDecomposedLoc(hashLocation);

    bool invalid = false;
    const unsigned lineNumber =
        _sourceManager.getLineNumber(fileID, offset, &invalid);
    assert(!invalid && "Error getting line number");

    if (!_includes.empty() && lineNumber > _lastLineNumber + 1) {
      _sortCurrent();
    }

    if (_includes.empty()) _firstLocation = hashLocation;
    _includes.emplace_back(filename, angled);
    _lastLineNumber = lineNumber;
    _lastLocation = range.getEnd();
  }

 private:
  void _sortCurrent() {
    const auto joined = sortIncludes(_includes, _reverse);
    clang::SourceRange range(_firstLocation, _lastLocation);
    _rewriter.ReplaceText(range, joined);
    _includes.clear();
  }

  clang::SourceLocation _firstLocation;
  clang::SourceLocation _lastLocation;
  unsigned _lastLineNumber;

  const clang::SourceManager& _sourceManager;
  clang::Rewriter& _rewriter;
  bool _reverse;

  llvm::SmallVector<Include, 8> _includes;
};

class Action : public clang::PreprocessOnlyAction {
 public:
  explicit Action(bool reverse) : _reverse(reverse) {
  }

  bool BeginInvocation(clang::CompilerInstance& compiler) override {
    _rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    return true;
  }

  bool BeginSourceFileAction(clang::CompilerInstance& compiler,
                             llvm::StringRef) override {
    auto hooks = std::make_unique<PreprocessorCallback>(_rewriter, _reverse);
    compiler.getPreprocessor().addPPCallbacks(std::move(hooks));
    return true;
  }

  void EndSourceFileAction() override {
    const auto fileID = _rewriter.getSourceMgr().getMainFileID();
    _rewriter.getEditBuffer(fileID).write(llvm::outs());
  }

 private:
  clang::Rewriter _rewriter;
  bool _reverse;
};
}  // namespace IncludeSorter


namespace {
llvm::cl::OptionCategory includeSorterCategory("minus-tool options");

llvm::cl::extrahelp includeSorterCategoryHelp(R"(
  Sorts your includes alphabetically.
)");

llvm::cl::opt<bool> reverseOption("reverse",
                                  llvm::cl::desc("Sort in reversed order"),
                                  llvm::cl::cat(includeSorterCategory));
}  // namespace

/// A custom \c FrontendActionFactory so that we can pass the options
/// to the constructor of the tool.
struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new IncludeSorter::Action(reverseOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, includeSorterCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(new ToolFactory());
}
