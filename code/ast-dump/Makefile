HEADERS := -isystem /llvm/include/
LIB_DIR := -L/llvm/lib/
TARGET := ast-dump
CXXFLAGS := -std=c++14

all: ast-dump

.phony: clean
.phony: run

clean:
	rm $(TARGET) || echo -n ""

ast-dump: $(TARGET).cpp
	$(CXX) $(HEADERS) $(LIB_DIR) $(CXXFLAGS) $(TARGET).cpp -lclang -o $(TARGET)
