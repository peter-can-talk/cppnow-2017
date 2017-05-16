CLANG_LIBS := \
	-Wl,--start-group \
	-lclangAST \
	-lclangASTMatchers \
	-lclangAnalysis \
	-lclangBasic \
	-lclangDriver \
	-lclangEdit \
	-lclangFrontend \
	-lclangFrontendTool \
	-lclangLex \
	-lclangParse \
	-lclangSema \
	-lclangEdit \
	-lclangRewrite \
	-lclangRewriteFrontend \
	-lclangSerialization \
	-lclangTooling \
	-lclangToolingCore \
	-Wl,--end-group

WARNINGS := -Wall

all: clang-variables

.phony: clean

clean:
	rm clang-variables || echo -n ""

clang-variables: clang-variables.cpp
	$(CXX) $(WARNINGS) -I/llvm/include -I/llvm/tools/clang/include -I/llvm/build/include -I/llvm/build/tools/clang/include -fno-exceptions -fno-rtti -std=c++1z -fno-rtti clang-variables.cpp `llvm-config --ldflags` ${CLANG_LIBS} `llvm-config --libs --system-libs` -o clang-variables
