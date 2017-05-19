// Clang includes
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// LLVM includes
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Standard includes
#include <memory>
#include <string>
#include <vector>

namespace ClangVariables {

/// Callback class for clang-variable matches.
class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  /// Handles the matched variable.
  ///
  /// Checks if the name of the matched variable is either empty or prefixed
  /// with `clang_` else emits a diagnostic and FixItHint.
  void run(const MatchResult& Result) {
    const clang::VarDecl* Variable =
        Result.Nodes.getNodeAs<clang::VarDecl>("clang");
    const llvm::StringRef Name = Variable->getName();

    if (Name.empty() || Name.startswith("clang_")) return;

    clang::DiagnosticsEngine& Engine = Result.Context->getDiagnostics();
    const unsigned ID =
        Engine.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                               "clang variable must have 'clang_' prefix");

    /// Hint to the user to prefix the variable with 'clang_'.
    const clang::FixItHint FixIt =
        clang::FixItHint::CreateInsertion(Variable->getLocation(), "clang_");

    Engine.Report(Variable->getLocation(), ID).AddFixItHint(FixIt);
  }
};  // namespace ClangVariables

/// Dispatches the ASTMatcher.
class Consumer : public clang::ASTConsumer {
 public:
  /// Creates the matcher for clang variables and dispatches it on the TU.
  void HandleTranslationUnit(clang::ASTContext& Context) override {
    using namespace clang::ast_matchers;  // NOLINT(build/namespaces)

    // clang-format off
    const auto Matcher = varDecl(
      isExpansionInMainFile(),
      hasType(isConstQualified()),                              // const
      hasInitializer(
        hasType(cxxRecordDecl(
          isLambda(),                                           // lambda
          has(functionTemplateDecl(                             // auto
            has(cxxMethodDecl(
              isNoThrow(),                                      // noexcept
              hasBody(compoundStmt(hasDescendant(gotoStmt())))  // goto
      )))))))).bind("clang");
    // clang-format on

    MatchHandler Handler;
    MatchFinder MatchFinder;
    MatchFinder.addMatcher(Matcher, &Handler);
    MatchFinder.matchAST(Context);
  }
};

/// Creates an `ASTConsumer` and logs begin and end of file processing.
class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  ASTConsumerPointer CreateASTConsumer(clang::CompilerInstance& Compiler,
                                       llvm::StringRef) override {
    return std::make_unique<Consumer>();
  }

  bool BeginSourceFileAction(clang::CompilerInstance& Compiler,
                             llvm::StringRef Filename) override {
    llvm::errs() << "Processing " << Filename << "\n\n";
    return true;
  }

  void EndSourceFileAction() override {
    llvm::errs() << "\nFinished processing file ...\n";
  }
};
}  // namespace ClangVariables

namespace {
llvm::cl::OptionCategory ToolCategory("clang-variables options");
llvm::cl::extrahelp MoreHelp(R"(
  Finds all Const Lambdas, that take an Auto parameter, are declared Noexcept
  and have a Goto statement inside, e.g.:

  const auto lambda = [] (auto) noexcept {
    bool done = true;
    flip: done = !done;
    if (!done) goto flip;
  }
)");

llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, ToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  const auto Action = newFrontendActionFactory<ClangVariables::Action>();
  return Tool.run(Action.get());
}
