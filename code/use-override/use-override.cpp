// Clang includes
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/RewriteBuffer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// LLVM includes
#include "llvm//Support/Path.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Standard includes
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UseOverride {
namespace {
bool isInSystemHeader(const clang::ASTContext& Context,
                      const clang::CXXMethodDecl& Method) {
  const clang::SourceManager& SourceManager = Context.getSourceManager();
  const clang::SourceLocation Location = Method.getLocation();
  return SourceManager.isInSystemHeader(Location);
}
}  // namespace

/// Visits all `CXXMethodDecl`s and checks for the `override` keyword.
class Checker : public clang::RecursiveASTVisitor<Checker> {
 public:
  /// Constructor.
  ///
  /// \param RewriteOption Whether to rewrite the source code.
  /// \param Rewriter A `clang::Rewriter` to possibly rewrite the source code.
  Checker(bool RewriteOption, clang::Rewriter& Rewriter)
  : Rewriter(Rewriter), RewriteOption(RewriteOption) {}

  /// Checks if a `CXXMethodDecl` should be marked `override` but is not.
  bool VisitCXXMethodDecl(clang::CXXMethodDecl* MethodDecl) {
    /// Can stop recursing if we are on a system node.
    if (isInSystemHeader(*Context, *MethodDecl)) return false;

    if (!needsOverride(*MethodDecl)) return true;

    auto& Diagnostics = Context->getDiagnostics();
    const auto ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "method '%0' should be declared override");

    clang::SourceLocation InsertionPoint = findInsertionPoint(*MethodDecl);

    clang::DiagnosticBuilder Diagnostic =
        Diagnostics.Report(InsertionPoint, ID);
    Diagnostic.AddString(MethodDecl->getName());

    if (RewriteOption) {
      Rewriter.InsertText(InsertionPoint, " override ");
    } else {
      const auto FixIt =
          clang::FixItHint::CreateInsertion(InsertionPoint, "override");
      Diagnostic.AddFixItHint(FixIt);
    }

    return true;
  }

  Checker& setContext(const clang::ASTContext& Context) {
    this->Context = &Context;
    return *this;
  }

 private:
  /// Determines whether the given `CXXMethodDecl` should be marked
  /// `override`.
  bool needsOverride(const clang::CXXMethodDecl& MethodDecl) {
    if (MethodDecl.size_overridden_methods() == 0) return false;
    const auto& Attrs = MethodDecl.getAttrs();
    return std::none_of(Attrs.begin(), Attrs.end(), [](const auto* Attr) {
      return Attr->getSpelling() == "override";
    });
  }

  /// Finds the `SourceLocation` for the end of the parameter list.
  ///
  /// For a function `void f() { }`, this will return the location just after
  /// the closing brace.
  clang::SourceLocation
  findInsertionPoint(const clang::CXXMethodDecl& MethodDecl) {
    clang::SourceLocation Location;

    /// Find the end of the parameter list.
    if (MethodDecl.param_empty()) {
      const unsigned Offset = MethodDecl.getName().size();
      Location = MethodDecl.getLocation().getLocWithOffset(Offset);
    } else {
      const clang::ParmVarDecl* Last = *std::prev(MethodDecl.param_end());
      Location = Last->getLocEnd();
    }

    // Given the current location, and the type of the token *just after* that
    // current location, finds the location just after *that next token*. So
    // if we have `f()` and we pass it the location of the opening
    // paranthesis, and say that the next token is the closing paranthesis
    // (`r_paren`), then we get the location just after that closing
    // paranthesis. Here we also skip any whitespace along the way, so we get
    // the location of the next token.
    Location = clang::Lexer::findLocationAfterToken(Location,
                                                    clang::tok::r_paren,
                                                    Context->getSourceManager(),
                                                    Context->getLangOpts(),
                                                    /*skipWhiteSpace=*/true);

    // We skipped whitespace, so ended up at the next token. We want the
    // position just before that next token.
    //   f() {          f() {
    // want ^   and not     ^
    return Location.getLocWithOffset(-1);
  }

  /// The `Rewriter` used to insert the `override` keyword.
  clang::Rewriter& Rewriter;

  /// The current `ASTContext`, needed for the `SourceManager` and `LangOpts`.
  const clang::ASTContext* Context;

  /// Whether the rewrite the code.
  bool RewriteOption;
};

/// Dispatches the `Checker` on a translation unit.
class Consumer : public clang::ASTConsumer {
 public:
  /// Constructor.
  ///
  /// Forwards all arguments to the Checker.
  template <typename... Args>
  explicit Consumer(Args&&... args) : Checker(std::forward<Args>(args)...) {}

  /// Dispatches the `Checker` on a translation unit.
  void HandleTranslationUnit(clang::ASTContext& Context) override {
    Checker.setContext(Context).TraverseDecl(Context.getTranslationUnitDecl());
  }

 private:
  /// The `Checker` to verify `override` usage.
  Checker Checker;
};

/// Creates the `ASTConsumer` and instantiates the `Rewriter`.
///
/// Also logs the start and end of processing for each file.
class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  explicit Action(bool RewriteOption) : RewriteOption(RewriteOption) {}

  ASTConsumerPointer CreateASTConsumer(clang::CompilerInstance& Compiler,
                                       llvm::StringRef Filename) override {
    Rewriter.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    return std::make_unique<Consumer>(RewriteOption, Rewriter);
  }

  bool BeginSourceFileAction(clang::CompilerInstance& Compiler,
                             llvm::StringRef Filename) override {
    llvm::errs() << "Processing " << Filename << "\n\n";
    return true;
  }

  void EndSourceFileAction() override {
    if (!RewriteOption) return;
    const auto File = Rewriter.getSourceMgr().getMainFileID();
    Rewriter.getEditBuffer(File).write(llvm::outs());
  }

 private:
  /// Whether to rewrite the source code. Forwarded to the `Consumer`.
  bool RewriteOption;

  /// A `clang::Rewriter` to rewrite source code. Forwarded to the `Consumer`.
  clang::Rewriter Rewriter;
};
}  // namespace UseOverride

namespace {
llvm::cl::OptionCategory UseOverrideCategory("use-override options");

llvm::cl::extrahelp UseOverrideHelp(R"(
This tool ensures that you use the 'override' keyword appropriately.
For example, given this snippet of code:

  struct Base {
    virtual void method(int);
  };

  struct Derived : public Base {
    void method(int);
  };

Running this tool over the code will produce a warning message stating that the
declaration 'method()' should be followed by the keyword 'override'.
)");

llvm::cl::opt<bool>
    RewriteOption("rewrite",
                  llvm::cl::init(false),
                  llvm::cl::desc("If set, emits rewritten source code"),
                  llvm::cl::cat(UseOverrideCategory));
llvm::cl::alias
    RewriteShortOption("r",
                       llvm::cl::desc("Alias for the --rewrite option"),
                       llvm::cl::aliasopt(RewriteOption));

llvm::cl::extrahelp
    CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}  // namespace

struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new UseOverride::Action(RewriteOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, UseOverrideCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(new ToolFactory());
}
