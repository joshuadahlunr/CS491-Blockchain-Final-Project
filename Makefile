INCLUDES = -Ithirdparty -Ithirdparty/Breep/include
LIBRARIES = -pthread -lboost_system
FLAGS = -std=c++20 -g

PROGRAM_NAME = tangle

DEPENDENCIES = src/main.cpp src/networking.o src/tangle.o src/keys.o thirdparty/cryptopp/libcryptopp.a

all: main
	echo "Project built successfully"

main: $(DEPENDENCIES)
	$(CXX) $(FLAGS) -o $(PROGRAM_NAME) src/main.cpp src/networking.o src/tangle.o src/keys.o thirdparty/cryptopp/libcryptopp.a $(LIBRARIES) $(INCLUDES)

%.o: %.cpp
	$(CXX) $(FLAGS) -c -o $@ $< $(LIBRARIES) $(INCLUDES)

clean:
	rm src/*.o $(PROGRAM_NAME)

thirdparty/cryptopp/libcryptopp.a:
	$(MAKE) -C thirdparty/cryptopp/ static
