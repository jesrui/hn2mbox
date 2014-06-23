CXXFLAGS = -std=c++11 -DNDEBUG -I. -Wall -Wno-unused-local-typedefs -O2 -msse2
LDFLAGS = -s

hn2mbox: hn2mbox.cpp

.PHONY: clean
clean:
	${RM} hn2mbox

