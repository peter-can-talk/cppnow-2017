#include <clang-c/Index.h>

#include <iostream>

struct RAIIString {
  explicit RAIIString(CXString cx_string) : cx_string(cx_string) {
  }

  ~RAIIString() {
    clang_disposeString(cx_string);
  }

  operator const char*() const noexcept {
    return clang_getCString(cx_string);
  }

  CXString cx_string;
};

struct Data {
  unsigned childOffset;
  std::string oldPrefix;
};

struct LineColumn {
  explicit LineColumn(CXSourceLocation location) {
    clang_getSpellingLocation(location,
                              /*file=*/NULL,
                              &line,
                              &column,
                              /*offset=*/NULL);
  }

  bool operator==(const LineColumn& other) const noexcept {
    return this->line == other.line && this->column == other.column;
  }

  bool operator!=(const LineColumn& other) const noexcept {
    return !(*this == other);
  }

  unsigned line;
  unsigned column;
};

void printRelativeLocation(LineColumn base, LineColumn location) {
  if (location.line == base.line) {
    std::cout << "col:" << location.column;
  } else {
    std::cout << "line:" << location.line << ":" << location.column;
  }
}

CXChildVisitResult countChildren(CXCursor, CXCursor, CXClientData data) {
  auto* count = reinterpret_cast<unsigned*>(data);
  *count += 1;
  return CXChildVisit_Continue;
}

CXChildVisitResult
dump(CXCursor cursor, CXCursor parent, CXClientData clientData) {
  CXSourceLocation location = clang_getCursorLocation(cursor);
  if (clang_Location_isInSystemHeader(location)) {
    return CXChildVisit_Continue;
  }

  auto* data = reinterpret_cast<Data*>(clientData);
  std::cout << data->oldPrefix;

  auto prefix = data->oldPrefix;
  if (data->childOffset == 1) {
    std::cout << "`-";
    prefix += "  ";
  } else {
    std::cout << "|-";
    prefix += "| ";
  }

  const CXCursorKind kind = clang_getCursorKind(cursor);
  std::cout << RAIIString(clang_getCursorKindSpelling(kind)) << " ";
  std::cout << clang_hashCursor(cursor) << " ";

  const CXSourceRange range = clang_getCursorExtent(cursor);
  LineColumn parentLocation(clang_getCursorLocation(parent));
  LineColumn start(clang_getRangeStart(range));
  LineColumn end(clang_getRangeEnd(range));
  end.column -= 1;  // exclusive -> inclusive

  std::cout << '<';
  printRelativeLocation(parentLocation, start);
  if (start != end) {
    std::cout << ", ";
    printRelativeLocation(start, end);
  }
  std::cout << "> ";
  printRelativeLocation(end, static_cast<LineColumn>(location));
  std::cout << " ";

  const CXCursor definition = clang_getCursorDefinition(cursor);
  if (!clang_Cursor_isNull(definition) &&
      !clang_equalCursors(cursor, definition)) {
    // This is a usage (DeclRefExpr), so print the hash again.
    std::cout << clang_hashCursor(definition) << " ";
  }

  std::cout << RAIIString(clang_getCursorSpelling(cursor)) << " ";

  const CXType type = clang_getCursorType(cursor);
  std::cout << RAIIString(clang_getTypeSpelling(type)) << "\n";

  unsigned numberOfChildren = 0;
  clang_visitChildren(cursor, countChildren, &numberOfChildren);

  if (numberOfChildren > 0) {
    Data nextData{numberOfChildren, prefix};
    clang_visitChildren(cursor, dump, &nextData);
  }

  data->childOffset -= 1;
  return CXChildVisit_Continue;
}

void walk(CXTranslationUnit tu) {
  CXCursor root = clang_getTranslationUnitCursor(tu);

  RAIIString kind(clang_getCursorKindSpelling(clang_getCursorKind(root)));
  std::cout << kind << '\n';

  unsigned numberOfChildren = 0;
  clang_visitChildren(root, countChildren, &numberOfChildren);

  Data rootData{numberOfChildren, ""};
  clang_visitChildren(root, dump, &rootData);
}

auto main(int argc, const char* argv[]) -> int {
  CXIndex index = clang_createIndex(true, true);

  CXTranslationUnit tu =
      clang_parseTranslationUnit(index, argv[1], nullptr, 0, nullptr, 0, 0);

  if (tu) {
    walk(tu);
    clang_disposeTranslationUnit(tu);
  } else {
    std::cout << "Could not parse '" << argv[1] << "'\n";
  }

  clang_disposeIndex(index);
}
