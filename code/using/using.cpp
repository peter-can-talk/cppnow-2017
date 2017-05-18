// Clang includes
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include "clang/Basic/Diagnostic.h"

// LLVM includes
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

// Standard includes
#include <cassert>

namespace UsingTool {

class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  void run(const MatchResult& Result) {
    const auto* Typedef = Result.Nodes.getNodeAs<clang::TypedefDecl>("typedef");
    assert(Typedef != nullptr);

    auto& Diagnostics = Result.Context->getDiagnostics();
    const unsigned ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "Prefer 'using' to 'typedef'");

    const auto Start = Typedef->getLocStart();
    const auto End = Start.getLocWithOffset(+7);
    const auto FixIt =
        clang::FixItHint::CreateReplacement({Start, End}, "using");

    clang::DiagnosticBuilder DB =
        Diagnostics.Report(Typedef->getLocation(), ID);
    DB.AddFixItHint(FixIt);
  }
};

class Consumer : public clang::ASTConsumer {
 public:
  void HandleTranslationUnit(clang::ASTContext& Context) {
    using namespace clang::ast_matchers;

    const auto Matcher = typedefDecl(isExpansionInMainFile()).bind("typedef");

    MatchHandler Handler;
    MatchFinder Finder;
    Finder.addMatcher(Matcher, &Handler);
    Finder.matchAST(Context);
  }
};

class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  ASTConsumerPointer
  CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<Consumer>();
  }
};
}  // namespace UsingTool

namespace {
llvm::cl::OptionCategory UsingToolCategory("UsingTool Options");

llvm::cl::extrahelp UsingToolCategoryHelp(R"(
    Verifies that you use `using` instead of `typedef`.
)");

}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, UsingToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto action = newFrontendActionFactory<UsingTool::Action>();
  return Tool.run(action.get());
}
