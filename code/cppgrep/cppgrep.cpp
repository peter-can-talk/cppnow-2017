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
#include <fstream>
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


class Filter {
 public:
  using Predicate = std::function<bool(CXCursor)>;

  explicit Filter(Predicate&& pattern) : _pattern(std::move(pattern)) {
  }

  void add(Predicate&& predicate) {
    _predicates.emplace_back(std::move(predicate));
  }

  bool matches(CXCursor cursor) const noexcept {
    if (!_pattern(cursor)) return false;
    if (_predicates.empty()) return true;

    // clang-format off
    return std::any_of(_predicates.begin(), _predicates.end(),
        [cursor](auto& predicate) { return predicate(cursor); });
    // clang-format on
  }

 private:
  Predicate _pattern;
  std::vector<Predicate> _predicates;
};

struct Data {
  using Lines = std::vector<std::string>;
  Data(Filter&& filter) : filter(std::move(filter)) {
  }

  Filter filter;
  Lines lines;
};

std::string toString(CXString cxString) {
  std::string string = clang_getCString(cxString);
  clang_disposeString(cxString);
  return string;
}

void displayMatch(CXSourceLocation location,
                  CXCursor cursor,
                  const Data::Lines& lines) {
  CXFile file;
  unsigned lineNumber, columnNumber;
  clang_getSpellingLocation(location,
                            &file,
                            &lineNumber,
                            &columnNumber,
                            nullptr);

  assert(lineNumber - 1 < lines.size());

  if (filesOption.size() > 1) {
    std::cout << toString(clang_getFileName(file)) << ':';
  }

  std::cout << "\033[1m" << lineNumber << ':' << columnNumber << "\033[0m: ";

  const auto& line = lines[lineNumber - 1];
  for (unsigned column = 1; column <= line.length(); ++column) {
    if (column == columnNumber) {
      const auto spelling = toString(clang_getCursorSpelling(cursor));
      std::cout << "\033[1;91m" << spelling << "\033[0m";
      column += spelling.length() - 1;
    } else {
      std::cout << line[column - 1];
    }
  }

  std::cout << '\n';
}

CXChildVisitResult grep(CXCursor cursor, CXCursor, CXClientData clientData) {
  const CXSourceLocation location = clang_getCursorLocation(cursor);
  if (clang_Location_isInSystemHeader(location)) {
    return CXChildVisit_Continue;
  }

  const auto* data = reinterpret_cast<Data*>(clientData);
  if (data->filter.matches(cursor)) {
    displayMatch(location, cursor, data->lines);
  }

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

Filter::Predicate makePatternPredicate() {
  auto regexOptions = std::regex::ECMAScript | std::regex::optimize;
  if (caseInsensitiveOption) regexOptions |= std::regex::icase;

  const std::regex pattern(patternOption, regexOptions);

  return [pattern](auto cursor) {
    const auto spelling = toString(clang_getCursorSpelling(cursor));
    return std::regex_search(spelling, pattern);
  };
}

Filter makeFilter() {
  Filter filter(makePatternPredicate());

  if (functionOption) {
    filter.add([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      if (memberOption) return kind == CXCursor_CXXMethod;
      return kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod;
    });
  }

  if (variableOption) {
    filter.add([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      if (memberOption) return kind == CXCursor_FieldDecl;
      return kind == CXCursor_VarDecl || kind == CXCursor_FieldDecl;
    });
  }

  if (parameterOption) {
    filter.add([](auto cursor) {
      return clang_getCursorKind(cursor) == CXCursor_ParmDecl;
    });
  }

  if (memberOption && !variableOption && !parameterOption) {
    filter.add([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      return kind == CXCursor_FieldDecl || kind == CXCursor_CXXMethod;
    });
  }

  if (recordOption) {
    filter.add([](auto cursor) {
      const auto kind = clang_getCursorKind(cursor);
      return kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl;
    });
  }

  return filter;
}

Data::Lines readLines(const std::string& filename) {
  Data::Lines lines;

  std::ifstream stream(filename);
  std::string line;
  while (std::getline(stream, line)) {
    lines.emplace_back(line);
  }

  return lines;
}

auto main(int argc, const char* argv[]) -> int {
  llvm::cl::HideUnrelatedOptions(cppGrepCategory);
  llvm::cl::ParseCommandLineOptions(argc, argv);

  Data data(makeFilter());

  CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/true,
                                    /*displayDiagnostics=*/true);
  for (const auto& filename : filesOption) {
    data.lines = readLines(filename);

    const CXTranslationUnit tu = parse(index, filename);
    if (!tu) break;

    auto cursor = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(cursor, grep, &data);

    clang_disposeTranslationUnit(tu);
  }
}
