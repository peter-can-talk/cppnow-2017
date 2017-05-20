// Clang includes
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
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

namespace EnableIfTool {

namespace {
bool isInSystemHeader(const clang::ASTContext& Context,
                      const clang::FunctionDecl& Function) {
  const clang::SourceManager& SourceManager = Context.getSourceManager();
  const clang::SourceLocation Location = Function.getLocation();
  return SourceManager.isInSystemHeader(Location);
}

bool hasEnableIfReturnType(clang::FunctionDecl* Function) {
  const clang::Type* BaseType = Function->getReturnType().getTypePtr();
  const auto* Type = llvm::dyn_cast<clang::DependentNameType>(BaseType);
  if (!Type) return false;

  const std::string Name = Type->getQualifier()
                               ->getAsType()
                               ->getAs<clang::TemplateSpecializationType>()
                               ->getTemplateName()
                               .getAsTemplateDecl()
                               ->getQualifiedNameAsString();

  return Name == "std::enable_if";
}
}  // namespace

/// Visits `FunctionDecl`s and checks for `std::enable_if`s on return types.
class Visitor : public clang::RecursiveASTVisitor<Visitor> {
 public:
  /// Constructor.
  ///
  /// Takes the `ASTContext` to retrieve the `SourceManager` later on.
  Visitor(clang::ASTContext& Context) : Context(Context) {}

  /// Visits a function declaration and fixes possible uses of `std::enable_if`.
  ///
  /// If the function does use `std::enable_if`, two fixits are emitted:
  ///   1. The first to replace `typename enable_if` with `enable_if_t`
  ///   2. The second to remove the `::type` at the end.
  bool VisitFunctionDecl(clang::FunctionDecl* Function) {
    if (isInSystemHeader(Context, *Function)) return true;
    if (!hasEnableIfReturnType(Function)) return true;

    clang::DiagnosticsEngine& Diagnostics = Context.getDiagnostics();
    const unsigned ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "Prefer 'enable_if_t' to 'enable_if'");

    const auto Range = Function->getReturnTypeSourceRange();
    clang::DiagnosticBuilder Builder = Diagnostics.Report(Range.getBegin(), ID);

    /// The first FixItHint replaces `typename std::enable_if` with
    /// `std::enable_if_t`.
    clang::SourceLocation Start = Range.getBegin();
    clang::SourceLocation End = Start.getLocWithOffset(+22);
    const auto FixItOne =
        clang::FixItHint::CreateReplacement({Start, End}, "std::enable_if_t");
    Builder.AddFixItHint(FixItOne);

    /// The second FixItHint replaces the `::type` at the end, since it is not
    /// needed with `std::enable_if_t`.
    Start = Range.getEnd().getLocWithOffset(-2);
    End = Range.getEnd();
    const auto FixItTwo = clang::FixItHint::CreateRemoval({Start, End});
    Builder.AddFixItHint(FixItTwo);

    return true;
  }

 private:
  clang::ASTContext& Context;
};

/// Simply creates a `Visitor` and dispatches it on the AST.
class Consumer : public clang::ASTConsumer {
 public:
  void HandleTranslationUnit(clang::ASTContext& Context) {
    Visitor(Context).TraverseDecl(Context.getTranslationUnitDecl());
  }
};

/// Creates the `ASTConsumer`.
class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  ASTConsumerPointer
  CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<Consumer>();
  }
};
}  // namespace EnableIfTool

namespace {
llvm::cl::OptionCategory EnableIfToolCategory("EnableIfTool Options");
llvm::cl::extrahelp EnableIfToolCategoryHelp(R"(
    Verifies that you use `std::enable_if_t` instead of `typename
    std::enable_if<...>::type` when using SFINAE on function return types.

    For example, given

    template<typename T>
    typename std::enable_if<std::is_integral<T>::value>::type
    add_one(T& value) {
      value += 1;
    }

    ...: warning: Prefer 'enable_if_t' to 'enable_if'
    typename std::enable_if<std::is_integral<T>::value>::type f(T& value) {
    ^~~~~~~~~~~~~~~~~~~~~~~                            ~~~~~~
    std::enable_if_t

)");
llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, EnableIfToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto action = newFrontendActionFactory<EnableIfTool::Action>();
  return Tool.run(action.get());
}
