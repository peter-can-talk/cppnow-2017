// Clang includes
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

// LLVM includes
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

// Standard includes
#include <cassert>

namespace McCabe {

class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  explicit MatchHandler(unsigned Threshold) : Threshold(Threshold) {
  }

  void run(const MatchResult& Result) {
    static const auto IntegerType =
        clang::DiagnosticsEngine::ArgumentKind::ak_uint;

    const auto* Function = Result.Nodes.getNodeAs<clang::FunctionDecl>("fn");
    assert(Function != nullptr);

    const auto CFG = clang::CFG::buildCFG(Function,
                                          Function->getBody(),
                                          Result.Context,
                                          clang::CFG::BuildOptions());

    const unsigned numberOfNodes = CFG->size() - 2;
    int numberOfEdges = -2;
    for (const auto* Block : *CFG) {
      numberOfEdges += Block->succ_size();
    }

    const unsigned Complexity = numberOfEdges - numberOfNodes + 2;
    if (Complexity <= Threshold) return;

    auto& Diagnostics = Result.Context->getDiagnostics();
    const auto ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "Function '%0' is too complex (%1)");

    auto Builder = Diagnostics.Report(Function->getLocation(), ID);
    Builder.AddString(Function->getQualifiedNameAsString());
    Builder.AddTaggedVal(Complexity, IntegerType);
  }

 private:
  unsigned Threshold;
};

class Consumer : public clang::ASTConsumer {
 public:
  template <typename... Args>
  explicit Consumer(Args&&... args) : Handler(std::forward<Args>(args)...) {
    using namespace clang::ast_matchers;
    const auto Matcher = functionDecl(isExpansionInMainFile()).bind("fn");
    Finder.addMatcher(Matcher, &Handler);
  }

  void HandleTranslationUnit(clang::ASTContext& Context) {
    Finder.matchAST(Context);
  }

 private:
  MatchHandler Handler;
  clang::ast_matchers::MatchFinder Finder;
};

class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  explicit Action(unsigned Threshold) : Threshold(Threshold) {
  }

  ASTConsumerPointer
  CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<Consumer>(Threshold);
  }

 private:
  unsigned Threshold;
};
}  // namespace McCabe

namespace {
llvm::cl::OptionCategory McCabeCategory("McCabe Options");

llvm::cl::extrahelp McCabeCategoryHelp(R"(
    Computes the McCabe (Cyclomatic) Complexity for each function in the given
    source files and emits a warning if the complexity is beyond a threshold.
)");

llvm::cl::opt<unsigned>
    ThresholdOption("threshold",
                    llvm::cl::init(2),
                    llvm::cl::desc("The threshold for emitting warnings"),
                    llvm::cl::cat(McCabeCategory));
llvm::cl::alias ShortThresholdOption("t",
                                     llvm::cl::desc("Alias for -threshold"),
                                     llvm::cl::aliasopt(ThresholdOption));

}  // namespace

struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new McCabe::Action(ThresholdOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, McCabeCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(new ToolFactory());
}
