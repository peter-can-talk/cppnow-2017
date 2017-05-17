TARGET := include-sorter
HEADERS := -isystem /llvm/include/
WARNINGS := -Wall -Wextra -pedantic
CXXFLAGS := $(WARNINGS) -std=c++1z -fno-exceptions -fno-rtti -O3 -Os
LDFLAGS := `llvm-config --ldflags`

CLANG_LIBS := \
	-lclangFrontendTool \
	-lclangRewriteFrontend \
	-lclangDynamicASTMatchers \
	-lclangTooling \
	-lclangFrontend \
	-lclangToolingCore \
	-lclangASTMatchers \
	-lclangParse \
	-lclangDriver \
	-lclangSerialization \
	-lclangRewrite \
	-lclangSema \
	-lclangEdit \
	-lclangAnalysis \
	-lclangAST \
	-lclangLex \
	-lclangBasic

LIBS := $(CLANG_LIBS) `llvm-config --libs --system-libs`

all: include-sorter

.phony: clean
.phony: run

clean:
	rm $(TARGET) || echo -n ""

include-sorter: $(TARGET).cpp
	$(CXX) $(HEADERS) $(LDFLAGS) $(CXXFLAGS) $(TARGET).cpp $(LIBS) -o $(TARGET)
