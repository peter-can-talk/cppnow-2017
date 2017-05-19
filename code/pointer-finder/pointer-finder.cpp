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

namespace PointerFinder {

/// Callback class for matches on the AST.
class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  /// Handles a match result for a pointer variable.
  ///
  /// Given a matched `DeclaratorDecl` (i.e. `VarDecl` or `FieldDecl`) with
  /// pointer type, verifies that if the variable is named, its name begins with
  /// a 'p_'. Otherwise emits a diagnostic and FixItHint.
  void run(const MatchResult& Result) {
    const auto* Decl = Result.Nodes.getNodeAs<clang::DeclaratorDecl>("decl");

    const llvm::StringRef Name = Decl->getName();

    /// The declaration may be unnamed (like `int*;`), so skip those.
    if (Name.empty() || Name.startswith("p_")) return;

    clang::DiagnosticsEngine& Diagnostics = Result.Context->getDiagnostics();
    const unsigned ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "pointer variable '%0' should "
                                    "have a 'p_' prefix");
    const auto FixIt =
        clang::FixItHint::CreateInsertion(Decl->getLocation(), "p_");

    clang::DiagnosticBuilder Builder =
        Diagnostics.Report(Decl->getLocation(), ID);
    Builder.AddString(Name);
    Builder.AddFixItHint(FixIt);
  }
};

/// Dispatches a a `MatchFinder` to look for pointer variables.
class Consumer : public clang::ASTConsumer {
 public:
  /// Registers a matcher on pointers and dispatches it on the AST.
  void HandleTranslationUnit(clang::ASTContext& Context) override {
    using namespace clang::ast_matchers;

    MatchFinder Finder;
    MatchHandler Handler;

    // We want to match variables or fields, i.e. both `DeclaratorDecl`s, that
    // are pointers. We want to skip variables in system headers of course. Note
    // that while `FunctionDecl`s are also `DeclaratorDecl`s, they will never
    // have pointer type and thus will not be matched. Function *pointers* will
    // still be matched, however.

    // clang-format off
      const auto Matcher =
          declaratorDecl(
            isExpansionInMainFile(),
            hasType(pointerType())
          ).bind("decl");
    // clang-format on

    Finder.addMatcher(Matcher, &Handler);
    Finder.matchAST(Context);
  }
};

/// Creates an `ASTConsumer` and logs begin and end of file processing.
class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  ASTConsumerPointer CreateASTConsumer(clang::CompilerInstance& Compiler,
                                       llvm::StringRef Filename) override {
    return std::make_unique<Consumer>();
  }

  bool BeginSourceFileAction(clang::CompilerInstance& Compiler,
                             llvm::StringRef Filename) override {
    llvm::outs() << "Processing file " << Filename << '\n';
    return true;
  }

  void EndSourceFileAction() override {
    llvm::outs() << "Done processing file ...\n";
  }
};
}  // namespace PointerFinder

namespace {
llvm::cl::extrahelp MoreHelp("\nMakes sure pointers have a 'p_' prefix\n");
llvm::cl::OptionCategory ToolCategory("PointerFinder");
llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, ToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto Action = newFrontendActionFactory<PointerFinder::Action>();
  return Tool.run(Action.get());
}
