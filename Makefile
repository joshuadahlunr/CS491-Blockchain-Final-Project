INCLUDES = -Ithirdparty -Ithirdparty/Breep/include
LIBRARIES = -pthread -lboost_system
FLAGS = -std=c++20

PROGRAM_NAME = tangle

all: main
	echo "Project built successfully"

main: main.cpp thirdparty/cryptopp/libcryptopp.a
	$(CXX) $(FLAGS) -o $(PROGRAM_NAME) main.cpp thirdparty/cryptopp/libcryptopp.a $(LIBRARIES) $(INCLUDES)

thirdparty/cryptopp/libcryptopp.a:
	$(MAKE) -C thirdparty/cryptopp/ static
