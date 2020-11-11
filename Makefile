
dev:
	make build && make run

build:
	clang++ -std=c++17 -g -O3 ast.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o tylang

run:
	./tylang

clean:
	rm -f tylang