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

class Visitor : public clang::RecursiveASTVisitor<Visitor> {
 public:
  Visitor(clang::ASTContext& Context) : Context(Context) {
  }

  bool VisitFunctionDecl(clang::FunctionDecl* Function) {
    const clang::SourceLocation Location = Function->getLocation();
    if (Context.getSourceManager().isInSystemHeader(Location)) return true;

    const auto* BaseType = Function->getReturnType().getTypePtr();
    const auto* Type = llvm::dyn_cast<clang::DependentNameType>(BaseType);
    if (!Type) return true;

    const std::string Name = Type->getQualifier()
                                 ->getAsType()
                                 ->getAs<clang::TemplateSpecializationType>()
                                 ->getTemplateName()
                                 .getAsTemplateDecl()
                                 ->getQualifiedNameAsString();

    if (Name != "std::enable_if") return true;

    auto& Diagnostics = Context.getDiagnostics();
    const auto ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "Prefer 'enable_if_t' to 'enable_if'");

    const auto ReturnTypeRange = Function->getReturnTypeSourceRange();
    auto Builder = Diagnostics.Report(ReturnTypeRange.getBegin(), ID);

    clang::SourceLocation Start = ReturnTypeRange.getBegin();
    clang::SourceLocation End = Start.getLocWithOffset(+22);
    const auto FixItOne =
        clang::FixItHint::CreateReplacement({Start, End}, "std::enable_if_t");
    Builder.AddFixItHint(FixItOne);

    Start = ReturnTypeRange.getEnd().getLocWithOffset(-2);
    End = Start.getLocWithOffset(+3);
    const auto FixItTwo = clang::FixItHint::CreateRemoval({Start, End});
    Builder.AddFixItHint(FixItTwo);

    return true;
  }

 private:
  clang::ASTContext& Context;
};

class Consumer : public clang::ASTConsumer {
 public:
  void HandleTranslationUnit(clang::ASTContext& Context) {
    Visitor(Context).TraverseDecl(Context.getTranslationUnitDecl());
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
}  // namespace EnableIfTool

namespace {
llvm::cl::OptionCategory EnableIfToolCategory("EnableIfTool Options");

llvm::cl::extrahelp EnableIfToolCategoryHelp(R"(
    Verifies that you use `enable_if_t` instead of `typename enable_if<...>::type`.
)");

}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, EnableIfToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto action = newFrontendActionFactory<EnableIfTool::Action>();
  return Tool.run(action.get());
}
