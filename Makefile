
all:
	node-gyp configure -std=c++0x
	node-gyp build -std=c++0x

clean:
	node-gyp clean

.PHONY: clean all
