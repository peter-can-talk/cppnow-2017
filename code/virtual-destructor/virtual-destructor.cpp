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

namespace VirtualDestructorTool {

class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  void run(const MatchResult& Result) {
    const auto* Destructor =
        Result.Nodes.getNodeAs<clang::CXXDestructorDecl>("destructor");
    assert(Destructor != nullptr);
    const auto* Derived =
        Result.Nodes.getNodeAs<clang::CXXRecordDecl>("derived");
    assert(Derived != nullptr);

    const clang::CXXRecordDecl* Class = Destructor->getParent();

    auto& Diagnostics = Result.Context->getDiagnostics();
    const char Message[] =
        "'%0' should have a virtual destructor "
        "because '%1' derives from it";
    const unsigned ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning, Message);

    auto Location = Destructor->isUserProvided() ? Destructor->getLocStart()
                                                 : Class->getLocation();

    clang::DiagnosticBuilder Builder = Diagnostics.Report(Location, ID);
    Builder.AddString(Class->getName());
    Builder.AddString(Derived->getName());

    if (Destructor->isUserProvided()) {
      const auto FixIt =
          clang::FixItHint::CreateInsertion(Location, "virtual ");
      Builder.AddFixItHint(FixIt);
    }
  }
};

class Consumer : public clang::ASTConsumer {
 public:
  void HandleTranslationUnit(clang::ASTContext& Context) {
    using namespace clang::ast_matchers;

    // clang-format off
    const auto Matcher =
        cxxRecordDecl(
          isDerivedFrom(
            cxxRecordDecl(
              has(cxxDestructorDecl(unless(isVirtual())
            ).bind("destructor"))))
          ).bind("derived");
    // clang-format on

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
}  // namespace VirtualDestructorTool

namespace {
llvm::cl::OptionCategory
    VirtualDestructorToolCategory("VirtualDestructorTool Options");

llvm::cl::extrahelp VirtualDestructorToolCategoryHelp(R"(
    Verifies that you declare destructors 'virtual' when necessary.
)");

}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, VirtualDestructorToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto action = newFrontendActionFactory<VirtualDestructorTool::Action>();
  return Tool.run(action.get());
}
