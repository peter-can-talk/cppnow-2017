TARGET := clang-variables
HEADERS := -isystem /llvm/include/
WARNINGS := -Wall -Wextra -pedantic -Wno-unused-parameter
CXXFLAGS := $(WARNINGS) -std=c++14 -fno-exceptions -fno-rtti -O3 -Os
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

all: clang-variables

.phony: clean
.phony: run

clean:
	rm $(TARGET) || echo -n ""

clang-variables: $(TARGET).cpp
	$(CXX) $(HEADERS) $(LDFLAGS) $(CXXFLAGS) $(TARGET).cpp $(LIBS) -o $(TARGET)
