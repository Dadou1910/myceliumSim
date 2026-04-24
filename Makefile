CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O2 -g3
LIBS     = -lsfml-graphics -lsfml-window -lsfml-system

TARGET = myceliumSim
SRC    = main.cpp simulation.cpp
CXXFLAGS += -pthread

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET)
