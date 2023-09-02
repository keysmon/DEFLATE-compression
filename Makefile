EXTRA_CXXFLAGS=
EXTRA_CFLAGS=
CXXFLAGS=-O3 -Wall -std=c++20 $(EXTRA_CXXFLAGS)
CFLAGS=-O3 -Wall -std=c18 $(EXTRA_CFLAGS)

all: uvgz

clean:
	rm -f uvgz *.o
