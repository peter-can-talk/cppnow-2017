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
#include <cassert>

class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  void run(const MatchResult& Result) {
    const clang::VarDecl* Variable =
        Result.Nodes.getNodeAs<clang::VarDecl>("clang");
    assert(Variable != nullptr);

    const clang::ASTContext* Context = Result.Context;
    const clang::SourceManager& SourceManager = Context->getSourceManager();

    if (SourceManager.isInSystemHeader(Variable->getLocation())) return;

    const llvm::StringRef Name = Variable->getName();

    if (!Name.empty() && !Name.startswith("clang_")) {
      clang::DiagnosticsEngine& Engine = Context->getDiagnostics();
      const unsigned ID = Engine.getCustomDiagID(
          clang::DiagnosticsEngine::Warning,
          "clang variable should be marked appropriately. lol this is so cool");

      const clang::FixItHint FixIt =
          clang::FixItHint::CreateInsertion(Variable->getLocation(), "clang_");

      Engine.Report(Variable->getLocation(), ID).AddFixItHint(FixIt);
    }
  }
};

class Consumer : public clang::ASTConsumer {
 public:
  Consumer() {
    using namespace clang::ast_matchers;  // NOLINT(build/namespaces)

    // clang-format off
    auto Matcher = varDecl(
      hasType(isConstQualified()),
      hasInitializer(
        hasType(cxxRecordDecl(
          isLambda(),
          has(functionTemplateDecl(
            has(cxxMethodDecl(
              isNoThrow(),
              hasBody(compoundStmt(hasDescendant(gotoStmt())))))))))))
      .bind("clang");
    // clang-format on

    MatchFinder.addMatcher(Matcher, &Handler);
  }

  void HandleTranslationUnit(clang::ASTContext& Context) override {
    MatchFinder.matchAST(Context);
  }

 private:
  clang::ast_matchers::MatchFinder MatchFinder;
  MatchHandler Handler;
};

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

namespace {
llvm::cl::OptionCategory ToolCategory("clang-variables options");

llvm::cl::extrahelp MoreHelp(R"(
)");

llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, ToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(newFrontendActionFactory<Action>().get());
}
