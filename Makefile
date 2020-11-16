
dev:
	make build && make run

build:
	g++ -std=c++17 -g -O3 main.cpp lexer/lexer.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o tylang
	# clang++ -std=c++17 -g -O3 ast.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o tylang

run:
	./tylang

clean:
	rm -f tylang