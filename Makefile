# Makefile for OS Project3: outputdevice.cpp

TAR = ex3.tar
TAR_CMD = tar cvf
CC = g++ -Wall -lpthread -std=c++11

all: lib

lib: outputdevice.o
	ar rvs liboutputdevice.a outputdevice.o

outputdevice: outputdevice.o
	$(CC) outputdevice.o -o outputdevice

outputdevice.o: outputdevice.cpp outputdevice.h
	$(CC) -c outputdevice.cpp -o outputdevice.o
	
clean:
	rm -f $(TAR) outputdevice.o liboutputdevice.a outputdevice 

tar: outputdevice.cpp Makefile README
	$(TAR_CMD) $(TAR) outputdevice.cpp Makefile README