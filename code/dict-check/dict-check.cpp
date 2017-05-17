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
#include <cassert>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>

namespace DictionaryCheck {

class Dictionary {
 public:
  explicit Dictionary(const std::string& filename) {
    std::ifstream stream(filename);
    for (std::string word; stream >> word;) {
      _words.insert(word);
    }

    // clang-format off
    llvm::errs() << "Read " << _words.size()
                 << " words from " << filename
                 << '\n';
    // clang-format on
  }

  bool contains(llvm::StringRef word) const {
    return _words.count(word);
  }

 private:
  llvm::StringSet<> _words;
};


class Checker : public clang::ast_matchers::MatchFinder::MatchCallback {
 public:
  using MatchResult = clang::ast_matchers::MatchFinder::MatchResult;

  explicit Checker(const std::string& dictionaryFile)
  : _dictionary(dictionaryFile) {
  }

  void run(const MatchResult& result) {
    const auto* target = result.Nodes.getNodeAs<clang::NamedDecl>("target");
    assert(target != nullptr);

    auto& diagnostics = result.Context->getDiagnostics();
    const auto name = target->getName();

    if (_dictionary.contains(name.lower())) return;

    const auto id =
        diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Warning,
                                    "The word '%0' is not in the dictionary");

    auto builder = diagnostics.Report(target->getLocation(), id);
    builder.AddString(name);

    const auto start = target->getLocation();
    const auto end = start.getLocWithOffset(name.size());
    const auto range = clang::CharSourceRange::getCharRange({start, end});
    builder.AddSourceRange(range);
  }

 private:
  Dictionary _dictionary;
};


class Consumer : public clang::ASTConsumer {
 public:
  Consumer(const std::string& dictionaryFile,
           bool includeFunctions,
           bool includeRecords)
  : _checker(dictionaryFile) {
    using namespace clang::ast_matchers;

    const auto variableMatcher =
        declaratorDecl(unless(functionDecl())).bind("target");
    _matchFinder.addMatcher(variableMatcher, &_checker);

    if (includeFunctions) {
      const auto functionMatcher = functionDecl().bind("target");
      _matchFinder.addMatcher(functionMatcher, &_checker);
    }

    if (includeRecords) {
      const auto recordMatcher =
          recordDecl(unless(isImplicit())).bind("target");
      _matchFinder.addMatcher(recordMatcher, &_checker);
    }
  }

  void HandleTranslationUnit(clang::ASTContext& context) override {
    _matchFinder.matchAST(context);
  }

 private:
  clang::ast_matchers::MatchFinder _matchFinder;

  Checker _checker;
};

class Action : public clang::ASTFrontendAction {
 public:
  using ASTConsumerPointer = std::unique_ptr<clang::ASTConsumer>;

  Action(const std::string& dictionaryFile,
         bool includeFunctions,
         bool includeRecords)
  : _dictionaryFile(dictionaryFile)
  , _includeFunctions(includeFunctions)
  , _includeRecords(includeRecords) {
  }

  ASTConsumerPointer
  CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) {
    return std::make_unique<Consumer>(_dictionaryFile,
                                      _includeFunctions,
                                      _includeRecords);
  }

 private:
  std::string _dictionaryFile;
  bool _includeFunctions;
  bool _includeRecords;
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
    dictionaryOption("dict",
                     llvm::cl::Required,
                     llvm::cl::desc("The dictionary file to load"),
                     llvm::cl::cat(DictionaryCheckCategory));

llvm::cl::opt<bool>
    functionsOption("functions",
                    llvm::cl::desc("Include function names in the check"),
                    llvm::cl::cat(DictionaryCheckCategory));

llvm::cl::opt<bool>
    recordsOption("records",
                  llvm::cl::desc("Include classes/structs/unions in the check"),
                  llvm::cl::cat(DictionaryCheckCategory));

}  // namespace


struct ToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new DictionaryCheck::Action(dictionaryOption,
                                       functionsOption,
                                       recordsOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
  using namespace clang::tooling;

  CommonOptionsParser OptionsParser(argc, argv, DictionaryCheckCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(new ToolFactory());
}
