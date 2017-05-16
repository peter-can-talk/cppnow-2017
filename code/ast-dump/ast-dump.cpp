#include <clang-c/Index.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

using LineColumn = std::pair<unsigned, unsigned>;

struct Data {
  unsigned childOffset;
  std::string oldPrefix;
};

std::string toString(CXString cxString) {
  std::string string = clang_getCString(cxString);
  clang_disposeString(cxString);
  return string;
}

LineColumn toLineColumn(CXSourceLocation location) {
  CXFile file;
  unsigned line, column, offset;
  clang_getSpellingLocation(location, &file, &line, &column, &offset);
  return {line, column};
}

void printRelativeLocation(LineColumn previous, LineColumn location) {
  if (location.first == previous.first) {
    std::cout << "col:" << location.second;
  } else {
    std::cout << "line:" << location.first << ":" << location.second;
  }
}

CXChildVisitResult
countChildren(CXCursor cursor, CXCursor parent, CXClientData data) {
  auto* count = reinterpret_cast<unsigned*>(data);
  *count += 1;
  return CXChildVisit_Continue;
}

CXChildVisitResult
visit(CXCursor cursor, CXCursor parent, CXClientData clientData) {
  CXSourceLocation location = clang_getCursorLocation(cursor);
  if (clang_Location_isInSystemHeader(location)) {
    return CXChildVisit_Continue;
  }

  auto* data = reinterpret_cast<Data*>(clientData);
  std::cout << data->oldPrefix;

  std::string prefix = data->oldPrefix;
  if (data->childOffset == 1) {
    std::cout << "`-";
    prefix += "  ";
  } else {
    std::cout << "|-";
    prefix += "| ";
  }

  const CXCursorKind kind = clang_getCursorKind(cursor);
  std::cout << toString(clang_getCursorKindSpelling(kind)) << " ";
  std::cout << clang_hashCursor(cursor) << " ";

  const CXSourceRange range = clang_getCursorExtent(cursor);
  auto parentLocation = toLineColumn(clang_getCursorLocation(parent));
  auto start = toLineColumn(clang_getRangeStart(range));
  auto end = toLineColumn(clang_getRangeEnd(range));
  end.second -= 1;

  std::cout << "<";
  printRelativeLocation(parentLocation, start);
  if (start != end) {
    std::cout << ", ";
    printRelativeLocation(start, end);
  }
  std::cout << "> ";
  printRelativeLocation(end, toLineColumn(location));
  std::cout << " ";

  const CXCursor definition = clang_getCursorDefinition(cursor);
  if (!clang_Cursor_isNull(definition) &&
      !clang_equalCursors(cursor, definition)) {
    std::cout << clang_hashCursor(definition) << " ";
  }

  std::cout << toString(clang_getCursorSpelling(cursor)) << " ";

  const CXType type = clang_getCursorType(cursor);
  std::cout << toString(clang_getTypeSpelling(type)) << " ";

  std::cout << std::endl;

  unsigned numberOfChildren = 0;
  clang_visitChildren(cursor, countChildren, &numberOfChildren);

  Data nextData{numberOfChildren, prefix};
  clang_visitChildren(cursor, visit, &nextData);

  data->childOffset -= 1;
  return CXChildVisit_Continue;
}

void traverse(CXTranslationUnit tu) {
  CXCursor root = clang_getTranslationUnitCursor(tu);

  CXCursorKind kind = clang_getCursorKind(root);
  std::cout << toString(clang_getCursorKindSpelling(kind)) << '\n';

  unsigned numberOfChildren = 0;
  clang_visitChildren(root, countChildren, &numberOfChildren);

  Data data{numberOfChildren, ""};
  clang_visitChildren(root, visit, &data);
}

auto main(int argc, const char* argv[]) -> int {
  CXIndex index = clang_createIndex(/*excludeDeclarationsFromPCH=*/true,
                                    /*displayDiagnostics=*/true);


  // See https://clang.llvm.org/doxygen/group__CINDEX__TRANSLATION__UNIT.html
  // for the possible options (last argument).
  CXTranslationUnit tu =
      clang_parseTranslationUnit(index,
                                 /*source_filename=*/argv[1],
                                 /*command_line_args=*/nullptr,
                                 /*num_command_line_args=*/0,
                                 /*unsaved_files=*/nullptr,
                                 /*num_unsaved_files=*/0,
                                 /*options=*/0);

  if (tu == nullptr) {
    std::cerr << "Error\n";
  } else {
    traverse(tu);
    clang_disposeTranslationUnit(tu);
  }

  clang_disposeIndex(index);
}
