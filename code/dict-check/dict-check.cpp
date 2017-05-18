// Clang includes
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

// LLVM includes
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Standard includes
#include <fstream>
#include <memory>
#include <string>

namespace DictionaryCheck {

using Dictionary = llvm::StringSet<>;

namespace {
Dictionary ReadWordsFromFile(const std::string& Filename) {
  std::ifstream Stream(Filename);
  if (!Stream.good()) {
    llvm::errs() << "Error reading from: " << Filename << '\n';
    return {};
  }

  // Assume one word per line.
  Dictionary Words;
  for (std::string Word; Stream >> Word;) {
    Words.insert(Word);
  }

  if (Words.empty()) {
    llvm::errs() << "Dictionary must not be empty!";
  } else {
    llvm::errs() << "Read " << Words.size() << " words from " << Filename
                 << '\n';
  }

  return Words;
}
}  // namespace

class Checker : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  explicit Checker(const Dictionary&& Words) : Words(std::move(Words)) {
  }

  void run(const MatchResult& Result) {
    const auto* Target = Result.Nodes.getNodeAs<clang::NamedDecl>("target");

    auto& Diagnostics = Result.Context->getDiagnostics();
    const auto Name = Target->getName();

    if (Words.count(Name.lower())) return;

    const auto ID =
        Diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "The word '%0' is not in the dictionary");

    auto Builder = Diagnostics.Report(Target->getLocation(), ID);
    Builder.AddString(Name);

    const auto Start = Target->getLocation();
    const auto End = Start.getLocWithOffset(Name.size());
    const auto Range = clang::CharSourceRange::getCharRange({Start, End});
    Builder.AddSourceRange(Range);
  }

 private:
  Dictionary Words;
};

class Consumer : public clang::ASTConsumer {
 public:
  Consumer(const Dictionary&& Words, bool IncludeFunctions, bool IncludeRecords)
  : Checker(std::move(Words)) {
    using namespace clang::ast_matchers;

    const auto VariableMatcher =
        declaratorDecl(unless(functionDecl())).bind("target");
    MatchFinder.addMatcher(VariableMatcher, &Checker);

    if (IncludeFunctions) {
      const auto FunctionMatcher = functionDecl().bind("target");
      MatchFinder.addMatcher(FunctionMatcher, &Checker);
    }

    if (IncludeRecords) {
      // Avoid implicit class name.
      const auto RecordMatcher =
          recordDecl(unless(isImplicit())).bind("target");
      MatchFinder.addMatcher(RecordMatcher, &Checker);
    }
  }

  void HandleTranslationUnit(clang::ASTContext& Context) override {
    MatchFinder.matchAST(Context);
  }

 private:
  clang::ast_matchers::MatchFinder MatchFinder;
  Checker Checker;
};

class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  Action(const std::string& DictionaryFile,
         bool IncludeFunctions,
         bool IncludeRecords)
  : DictionaryFile(DictionaryFile)
  , IncludeFunctions(IncludeFunctions)
  , IncludeRecords(IncludeRecords) {
  }

  ASTConsumerPointer CreateASTConsumer(clang::CompilerInstance& Compiler,
                                       llvm::StringRef Filename) {
    const auto Words = ReadWordsFromFile(DictionaryFile);
    if (Words.empty()) return nullptr;
    return std::make_unique<Consumer>(std::move(Words),
                                      IncludeFunctions,
                                      IncludeRecords);
  }

 private:
  std::string DictionaryFile;
  bool IncludeFunctions;
  bool IncludeRecords;
};
}  // namespace DictionaryCheck

namespace {
llvm::cl::OptionCategory DictionaryCheckCategory("DictionaryCheck Options");

llvm::cl::extrahelp DictionaryCheckCategoryHelp(R"(
  This tool verifies that you use readable names for your variables, functions,
  classes and other entities by performing a case-insensitive dictionary check
  on each name.
  )");

llvm::cl::opt<std::string>
    DictionaryOption("dict",
                     llvm::cl::Required,
                     llvm::cl::desc("The dictionary file to load"),
                     llvm::cl::cat(DictionaryCheckCategory));
llvm::cl::alias
    DictionaryShortOption("d",
                          llvm::cl::desc("Alias for the --dict option"),
                          llvm::cl::aliasopt(DictionaryOption));

llvm::cl::opt<bool>
    FunctionsOption("functions",
                    llvm::cl::desc("Include function names in the check"),
                    llvm::cl::cat(DictionaryCheckCategory));
llvm::cl::alias
    FunctionShortOption("f",
                        llvm::cl::desc("Alias for the --functions option"),
                        llvm::cl::aliasopt(FunctionsOption));

llvm::cl::opt<bool>
    RecordsOption("records",
                  llvm::cl::desc("Include classes/structs/unions in the check"),
                  llvm::cl::cat(DictionaryCheckCategory));
llvm::cl::alias
    RecordsShortOption("r",
                       llvm::cl::desc("Alias for the --records option"),
                       llvm::cl::aliasopt(RecordsOption));

}  // namespace


struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new DictionaryCheck::Action(DictionaryOption,
                                       FunctionsOption,
                                       RecordsOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, DictionaryCheckCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(new ToolFactory());
}
