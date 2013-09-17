all : main

include ../buildconfig

main: 
	g++  $(INCLUDES) $(BUIL_FLAGS) -c -MMD main.cpp -o main.o ;
	g++  -O3 -rdynamic -Wl,-R -Wl, $(LIB_PATH) -o main main.o $(LIBS_CLANG) $(LIBS_LLVM) $(LIBS_GENERAL)
	
clean:
	rm -v ./main ./main.o