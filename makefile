CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17

.PHONY: all clean

TARGET1 = ppcbc
TARGET2 = ppcbs

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(TARGET1).o common.o
$(TARGET2): $(TARGET2).o common.o

ppcbc.o: ppcbc.c protconst.h
ppcbs.o: ppcbs.c protconst.h
common.o: common.c

clean:
	rm -f $(TARGET1) $(TARGET2) *.o *~