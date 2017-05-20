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
#include <llvm/ADT/StringSet.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

namespace VirtualDestructorTool {

/// Handles all matched classes and emits diagnostics when appropriate.
class MatchHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  void run(const MatchResult& Result) {
    const auto* Destructor =
        Result.Nodes.getNodeAs<clang::CXXDestructorDecl>("destructor");

    const clang::CXXRecordDecl* Base = Destructor->getParent();

    // Insert the new name of the base class or return if we've seen it already.
    const std::string BaseName = Base->getQualifiedNameAsString();
    if (auto[_, Success] = BaseNames.insert(BaseName); !Success) {
      return;
    }

    const auto* Derived =
        Result.Nodes.getNodeAs<clang::CXXRecordDecl>("derived");

    clang::DiagnosticsEngine& Diagnostics = Result.Context->getDiagnostics();
    const char Message[] =
        "'%0' should have a virtual destructor because '%1' derives from it";
    const unsigned ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning, Message);

    // We can even warn about missing virtual when the user forgot to declare
    // the destructor alltogether! In that case, the diagnostic should point to
    // the class declaration instead of the destructor declaration.
    auto Location = Destructor->isUserProvided() ? Destructor->getLocStart()
                                                 : Base->getLocation();

    clang::DiagnosticBuilder Builder = Diagnostics.Report(Location, ID);
    Builder.AddString(BaseName);
    Builder.AddString(Derived->getQualifiedNameAsString());

    // If the destructor is user-provided, we also recommend a FixItHint.
    if (Destructor->isUserProvided()) {
      const clang::FixItHint FixIt =
          clang::FixItHint::CreateInsertion(Location, "virtual ");
      Builder.AddFixItHint(FixIt);
    }
  }

 private:
  // A set of all base-class names seen so far, so we avoid duplicate warnings.
  llvm::StringSet<> BaseNames;
};

class Consumer : public clang::ASTConsumer {
 public:
  /// Creates the `ASTMatcher` to match destructors and dispatches it on the TU.
  void HandleTranslationUnit(clang::ASTContext& Context) {
    using namespace clang::ast_matchers;

    // Want to match all classes, that are derived from classe, that have a
    // destructor tha tis not virtual. This leaves nothing to be done in the
    // `MatchHandler` than emitting a diagnostics.

    // clang-format off
    const auto Matcher =
        cxxRecordDecl(
          isExpansionInMainFile(),
          isDerivedFrom(
            cxxRecordDecl(
              has(cxxDestructorDecl(
                unless(isVirtual())
              ).bind("destructor"))))
          ).bind("derived");
    // clang-format on

    MatchHandler Handler;
    MatchFinder Finder;
    Finder.addMatcher(Matcher, &Handler);
    Finder.matchAST(Context);
  }
};

/// Creates an `ASTConsumer` that defines the matcher.
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
    Verifies that destructors are declared 'virtual' in case at least one class
    derives from it. Also warns about a missing destructor if no user-provided
    destructor was ever declared.
)");

}  // namespace

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, VirtualDestructorToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  auto Action = newFrontendActionFactory<VirtualDestructorTool::Action>();
  return Tool.run(Action.get());
}
