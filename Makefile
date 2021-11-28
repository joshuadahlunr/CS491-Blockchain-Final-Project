INCLUDES = -Ithirdparty -Ithirdparty/Breep/include
LIBRARIES = -pthread -lboost_system
FLAGS = -std=c++20 -g

PROGRAM_NAME = tangle

DEPENDENCIES = src/main.o src/networking_handshake.o src/networking_tangle.o src/tangle.o src/transaction.o src/keys.o thirdparty/cryptopp/libcryptopp.a

all: main
	echo "Project built successfully"

main: $(DEPENDENCIES)
	$(CXX) $(FLAGS) -o $(PROGRAM_NAME) $(DEPENDENCIES) $(LIBRARIES) $(INCLUDES)

%.o: %.cpp
	$(CXX) $(FLAGS) -c -o $@ $< $(LIBRARIES) $(INCLUDES)

# Header file dependencies
src/keys.o: src/keys.hpp
src/transaction.o: src/transaction.hpp src/utility.hpp src/keys.hpp
src/tangle.o: src/tangle.hpp src/transaction.hpp src/utility.hpp src/keys.hpp
src/networking_handshake.o: src/networking.hpp src/tangle.hpp src/transaction.hpp src/utility.hpp src/keys.hpp
src/networking_tangle.o: src/networking.hpp src/tangle.hpp src/transaction.hpp src/utility.hpp src/keys.hpp
src/main.o: src/networking.hpp src/tangle.hpp src/transaction.hpp src/utility.hpp src/keys.hpp

clean:
	rm src/*.o $(PROGRAM_NAME)

thirdparty/cryptopp/libcryptopp.a:
	$(MAKE) -C thirdparty/cryptopp/ static
