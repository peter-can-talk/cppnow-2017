// clang include
#include <clang-c/Index.h>

// LLVM includes
#include <llvm/Support/CommandLine.h>

// Clang includes
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/CompilationDatabase.h>


// Standard includes
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <vector>


namespace {
llvm::cl::OptionCategory cppGrepCategory("CppGrep Options");


llvm::cl::opt<std::string> patternOption(llvm::cl::Positional,
                                         llvm::cl::Required,
                                         llvm::cl::desc("<pattern>"));

llvm::cl::list<std::string> filesOption(llvm::cl::Positional,
                                        llvm::cl::OneOrMore,
                                        llvm::cl::desc("<file> [files...]"));

llvm::cl::opt<bool>
    caseInsensitiveOption("i",
                          llvm::cl::desc("Make the search case-insensitive"),
                          llvm::cl::cat(cppGrepCategory));

llvm::cl::opt<bool> functionOption("function",
                                   llvm::cl::desc("Filter by functions"),
                                   llvm::cl::cat(cppGrepCategory));
llvm::cl::alias functionShortOption("f",
                                    llvm::cl::desc("Alias for -function"),
                                    llvm::cl::aliasopt(functionOption));

llvm::cl::opt<bool> variableOption("variable",
                                   llvm::cl::desc("Filter by variables"),
                                   llvm::cl::cat(cppGrepCategory));
llvm::cl::alias variableShortOption("v",
                                    llvm::cl::desc("Alias for -variable"),
                                    llvm::cl::aliasopt(variableOption));

llvm::cl::opt<bool>
    recordOption("record",
                 llvm::cl::desc("Filter by records (class/struct)"),
                 llvm::cl::cat(cppGrepCategory));
llvm::cl::alias recordShortOption("r",
                                  llvm::cl::desc("Alias for -record"),
                                  llvm::cl::aliasopt(recordOption));

llvm::cl::opt<bool>
    parameterOption("parameter",
                    llvm::cl::desc("Filter by function parameter"),
                    llvm::cl::cat(cppGrepCategory));
llvm::cl::alias parameterShortOption("p",
                                     llvm::cl::desc("Alias for -parameter"),
                                     llvm::cl::aliasopt(parameterOption));

llvm::cl::opt<bool> memberOption("member",
                                 llvm::cl::desc("Filter by members"),
                                 llvm::cl::cat(cppGrepCategory));
llvm::cl::alias memberShortOption("m",
                                  llvm::cl::desc("Alias for -member"),
                                  llvm::cl::aliasopt(memberOption));
}  // namespace


using Predicate = std::function<bool(CXCursor)>;


std::string toString(CXString cxString) {
  std::string string = clang_getCString(cxString);
  clang_disposeString(cxString);
  return string;
}

void displayMatch(CXSourceLocation location, CXCursor cursor) {
  CXFile file;
  unsigned line, column;
  clang_getSpellingLocation(location, &file, &line, &column, nullptr);

  if (filesOption.size() > 1) {
    std::cout << toString(clang_getFileName(file)) << ':';
  }

  std::cout << line << ':' << column << ": ";

  const auto tu = clang_Cursor_getTranslationUnit(cursor);
  const CXSourceRange range = clang_getCursorExtent(cursor);
  assert(!clang_Range_isNull(range));

  CXToken* tokens;
  unsigned numberOfTokens;
  clang_tokenize(tu, range, &tokens, &numberOfTokens);

  for (unsigned index = 0; index < numberOfTokens; ++index) {
    const auto tokenLocation = clang_getTokenLocation(tu, tokens[index]);
    if (clang_equalLocations(tokenLocation, location)) {
      if (index > 0) {
        std::cout << toString(clang_getTokenSpelling(tu, tokens[index - 1]))
                  << ' ';
      }

      const auto spelling = toString(clang_getTokenSpelling(tu, tokens[index]));
      std::cout << "\033[91m" << spelling << "\033[0m";

      if (index + 1 < numberOfTokens) {
        std::cout << ' '
                  << toString(clang_getTokenSpelling(tu, tokens[index + 1]));
      }

      std::cout << '\n';
      break;
    }
  }

  clang_disposeTokens(tu, tokens, numberOfTokens);
}

CXChildVisitResult grep(CXCursor cursor, CXCursor, CXClientData data) {
  const CXSourceLocation location = clang_getCursorLocation(cursor);
  if (clang_Location_isInSystemHeader(location)) {
    return CXChildVisit_Continue;
  }

  const auto* predicates = reinterpret_cast<std::vector<Predicate>*>(data);
  for (const auto& predicate : *predicates) {
    if (!predicate(cursor)) return CXChildVisit_Recurse;
  }

  displayMatch(location, cursor);

  return CXChildVisit_Recurse;
}

CXTranslationUnit parse(CXIndex index, const std::string& filename) {
  CXTranslationUnit tu =
      clang_parseTranslationUnit(index,
                                 /*source_filename=*/filename.c_str(),
                                 /*command_line_args=*/nullptr,
                                 /*num_command_line_args=*/0,
                                 /*unsaved_files=*/nullptr,
                                 /*num_unsaved_files=*/0,
                                 /*options=*/0);
  if (!tu) {
    std::cerr << "Error parsing file: '" << filename << "'\n";
  }

  return tu;
}

Predicate makePatternPredicate() {
  auto regexOptions = std::regex::ECMAScript | std::regex::optimize;
  if (caseInsensitiveOption) regexOptions |= std::regex::icase;

  const std::regex pattern(patternOption, regexOptions);

  return [pattern](auto cursor) {
    const auto spelling = toString(clang_getCursorSpelling(cursor));
    return std::regex_search(spelling, pattern);
  };
}

std::vector<Predicate> getOptionPredicates() {
  std::vector<Predicate> predicates;

  predicates.emplace_back(makePatternPredicate());

  if (functionOption) {
    predicates.emplace_back([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      if (memberOption) return kind == CXCursor_CXXMethod;
      return kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod;
    });
  }

  if (variableOption) {
    predicates.emplace_back([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      if (memberOption) return kind == CXCursor_FieldDecl;
      return kind == CXCursor_VarDecl || kind == CXCursor_FieldDecl;
    });
  }

  if (parameterOption) {
    predicates.emplace_back([](auto cursor) {
      return clang_getCursorKind(cursor) == CXCursor_ParmDecl;
    });
  }

  if (memberOption && !variableOption && !parameterOption) {
    predicates.emplace_back([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      return kind == CXCursor_FieldDecl || kind == CXCursor_CXXMethod;
    });
  }

  if (recordOption) {
    predicates.emplace_back([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      return kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl;
    });
  }

  return predicates;
}

auto main(int argc, const char* argv[]) -> int {
  llvm::cl::HideUnrelatedOptions(cppGrepCategory);
  llvm::cl::ParseCommandLineOptions(argc, argv);
  auto predicates = getOptionPredicates();

  CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/true,
                                    /*displayDiagnostics=*/true);

  for (const auto& filename : filesOption) {
    const CXTranslationUnit tu = parse(index, filename);
    if (!tu) break;
    auto cursor = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(cursor, grep, &predicates);
    clang_disposeTranslationUnit(tu);
  }
}
