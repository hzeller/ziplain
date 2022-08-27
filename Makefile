CXXFLAGS=-std=c++17 -O2 -W -Wall -Wextra -Wpedantic

demo: demo.o ziplain.o
	$(CXX) -o$@ $^ -lz

ziplain.o: ziplain.cc

clean:
	rm -f *.o demo
