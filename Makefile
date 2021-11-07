INCLUDES = -Ithirdparty -Ithirdparty/Breep/include
LIBRARIES = -pthread -lboost_system
FLAGS = -std=c++20 -g

PROGRAM_NAME = tangle

all: main
	echo "Project built successfully"

main: src/main.cpp src/networking.o thirdparty/cryptopp/libcryptopp.a
	$(CXX) $(FLAGS) -o $(PROGRAM_NAME) src/main.cpp src/networking.o thirdparty/cryptopp/libcryptopp.a $(LIBRARIES) $(INCLUDES)

%.o: %.cpp thirdparty/cryptopp/libcryptopp.a
	$(CXX) $(FLAGS) -c -o $@ $< $(LIBRARIES) $(INCLUDES)


thirdparty/cryptopp/libcryptopp.a:
	$(MAKE) -C thirdparty/cryptopp/ static
