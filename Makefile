CC=g++
CFLAGS=-MMD -Wall -Wextra -pedantic -std=c++17

SRCC := $(wildcard src/*.c)
SRCPP := $(wildcard src/*.cpp)
OBJ := $(SRCC:%.c=%.o) $(SRCPP:%.cpp=%.o)
DEP=$(OBJ:%.o=%.d)

EXE=OnymosStockTradingEngine_Simulation.exe
LIBS=$(addprefix -l,)

TARGET=/

all: debug

debug: CFLAGS += -g
debug: $(EXE)

remake: clean debug
.NOTPARALLEL: remake

release: CFLAGS += -O3 -DNDEBUG
release: clean $(EXE)
.NOTPARALLEL: release

clean:
	rm -f $(OBJ) $(DEP) $(EXE)

install: all
	cp $(EXE) $(TARGET)/bin

$(EXE): $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

-include $(DEP)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<