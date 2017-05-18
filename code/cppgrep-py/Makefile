TARGET := cppgrep.py
LIB_PATH := ~/Documents/Projects/llvm/build/lib
MODULE_PATH := ~/Documents/Projects/llvm/tools/clang/bindings/python

.phony: run

run:
	DYLD_LIBRARY_PATH=$(LIB_PATH) \
	PYTHONPATH=$(PYTHONPATH):$(MODULE_PATH) \
	python $(TARGET) $(ARGS)
