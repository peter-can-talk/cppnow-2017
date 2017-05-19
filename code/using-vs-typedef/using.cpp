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

namespace UsingTool {

/// Acts on each `typedef` by emitting a diagnostic and FixItHint.
class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  /// Warns about the use of `typedef` and recommends `using` via a `FixItHint`.
  void run(const MatchResult& Result) {
    const auto* Typedef = Result.Nodes.getNodeAs<clang::TypedefDecl>("typedef");

    clang::DiagnosticsEngine& Diagnostics = Result.Context->getDiagnostics();
    const unsigned ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "Prefer 'using' to 'typedef'");

    const auto UsingString =
        (llvm::Twine("using ") + Typedef->getName() + " = ...").str();
    const clang::SourceRange Range = Typedef->getSourceRange();
    const auto FixIt = clang::FixItHint::CreateReplacement(Range, UsingString);

    // Note: getLocation() points to the start of the typedef'd name,
    // e.g. `MyInt` in `typedef int MyInt`. So use `getLocStart()` instead.
    Diagnostics.Report(Typedef->getLocStart(), ID).AddFixItHint(FixIt);
  }
};

/// Consumes a translation unit by dispatching an `ASTMatcher` on it.
class Consumer : public clang::ASTConsumer {
 public:
  /// Creates an `ASTMatcher` and dispatches it on the AST.
  void HandleTranslationUnit(clang::ASTContext& Context) {
    using namespace clang::ast_matchers;

    /// Could also use a RecursiveASTVisitor and VisitTypedefDecl.
    const auto Matcher = typedefDecl(isExpansionInMainFile()).bind("typedef");

    MatchHandler Handler;
    MatchFinder Finder;
    Finder.addMatcher(Matcher, &Handler);
    Finder.matchAST(Context);
  }
};

/// Creates an `ASTConsumer`.
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

    For example, given this declaration:

    typedef int MyInt;

    This tool will emit

    ...: warning: Prefer 'using' to 'typedef'
    typedef int MyInt;
    ~~~~~~~     ^
    using
)");

llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, UsingToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto action = newFrontendActionFactory<UsingTool::Action>();
  return Tool.run(action.get());
}
