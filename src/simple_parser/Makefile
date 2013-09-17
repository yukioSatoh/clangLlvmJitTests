all : main

include ../buildconfig

SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
TARGET=simple_parser

all: $(SOURCES) $(TARGET)

$(TARGET): $(OBJECTS)
	g++  -O3 -rdynamic -Wl,-R -Wl, $(LIB_PATH) -o $(TARGET) $(OBJECTS) $(LIBS_CLANG) $(LIBS_LLVM) $(LIBS_GENERAL)

.cpp.o:
	g++  $(INCLUDES) $(BUIL_FLAGS) -c -MMD $< -o $@
	
clean:
	rm -rf *o $(TARGET)