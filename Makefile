CPP=g++
CPPFLAGS = -Werror
override CPPFLAGS+=-O3 -Wall -Wextra -Wpedantic -I$(INC) -std=c++17 -pthread


SRC=src
INC=include
OBJ=obj
BIN=packet_generator
SRCS=$(wildcard $(SRC)/*.cpp)
OBJS=$(patsubst $(SRC)/%.cpp, $(OBJ)/%.o, $(SRCS))


.PHONY: all clean

all: $(OBJ) $(BIN)

packet_generator: $(OBJS)
	$(CPP) $(CPPFLAGS) $^ -o $@

$(OBJ)/%.o: $(SRC)/%.cpp
	$(CPP) $(CPPFLAGS) -c $^ -o $@

clean:
	rm -rf $(OBJ) $(BIN)

$(OBJ):
	mkdir $(OBJ)
