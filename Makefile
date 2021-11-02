INCLUDES = -Ithirdparty/Breep/include
LIBRARIES = -pthread -lboost_system
FLAGS = -std=c++20

PROGRAM_NAME = tangle

all: main
	echo "Project built successfully"

main: main.cpp
	$(CXX) $(FLAGS) -o $(PROGRAM_NAME) main.cpp $(LIBRARIES) $(INCLUDES)
