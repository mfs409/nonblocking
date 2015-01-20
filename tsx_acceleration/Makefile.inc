CXXFLAGS += -O3
CXXFLAGS += -m32
CXXFLAGS += -msse2 -mfpmath=sse -march=native -mtune=native
SUFFIX = x86
LDFLAGS += -m32
LDFLAGS += -lpthread -lrt
