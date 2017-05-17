// Clang includes
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// LLVM includes
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Standard includes
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>


namespace MinusTool {

class Handler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  Handler(bool rewrite, const std::string& rewriteSuffix)
  : _rewrite(rewrite), _rewriteSuffix(rewriteSuffix) {
  }

  void run(const MatchResult& result) {
    auto& context = *result.Context;

    const auto& op = result.Nodes.getNodeAs<clang::BinaryOperator>("op");
    assert(op != nullptr && "Expected to have binary operator");

    const auto start = op->getOperatorLoc();
    const auto end = start.getLocWithOffset(+1);
    const auto fixit = clang::FixItHint::CreateReplacement({start, end}, "-");

    auto& diagnostics = context.getDiagnostics();
    const auto id =
        diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "This should be a minus!!");

    diagnostics.Report(start, id).AddFixItHint(fixit);
  }

 private:
  bool _rewrite;
  std::string _rewriteSuffix;
};

class Consumer : public clang::ASTConsumer {
 public:
  template <typename... Args>
  Consumer(Args&&... args) : _handler(std::forward<Args>(args)...) {
  }

  void HandleTranslationUnit(clang::ASTContext& context) override {
    using namespace clang::ast_matchers;  // NOLINT

    // clang-format off
    const auto matcher =
      binaryOperator(
        hasLHS(integerLiteral().bind("lhs")),
        hasRHS(integerLiteral().bind("rhs")),
        hasOperatorName("+")
      ).bind("op");
    // clang-format on

    MatchFinder finder;
    finder.addMatcher(matcher, &_handler);
    finder.matchAST(context);
  }

 private:
  Handler _handler;
};

class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  Action(bool rewrite, const std::string& rewriteSuffix)
  : _rewrite(rewrite), _rewriteSuffix(rewriteSuffix) {
  }

  ASTConsumerPointer
  CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<Consumer>(_rewrite, _rewriteSuffix);
  }

 private:
  bool _rewrite;
  std::string _rewriteSuffix;
};

}  // namespace MinusTool

namespace {
llvm::cl::OptionCategory MinusToolCategory("minus-tool options");

llvm::cl::extrahelp MinusToolCategoryHelp(R"(
This tool turns all your plusses into minuses, because why not.
Given a binary plus operation with two integer operands:

int x = 4 + 2;

This tool will rewrite the code to change the plus into a minus:

int x = 4 - 2;
)");

llvm::cl::opt<bool>
    rewriteOption("rewrite",
                  llvm::cl::desc("If set, emits rewritten source code"),
                  llvm::cl::cat(MinusToolCategory));

llvm::cl::opt<std::string> rewriteSuffixOption(
    "rewrite-suffix",
    llvm::cl::desc("If -rewrite is set, changes will be rewritten to a file "
                   "with the same name, but this suffix"),
    llvm::cl::cat(MinusToolCategory));

llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new MinusTool::Action(rewriteOption, rewriteSuffixOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser optionsParser(argc, argv, MinusToolCategory);
  ClangTool tool(optionsParser.getCompilations(),
                 optionsParser.getSourcePathList());

  llvm::outs() << optionsParser.getSourcePathList().size() << '\n';

  return tool.run(new ToolFactory());
}
