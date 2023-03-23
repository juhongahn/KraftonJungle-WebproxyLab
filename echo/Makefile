CC = gcc
CFLAGS = -O2 -Wall -I .

# This flag includes the Pthreads library on a Linux box.
# Others systems will probably require something different.
LIB = -lpthread
TARGET = echoclient echoserver

all: $(TARGET)

echoclient: csapp.o
	$(CC) $(CFLAGS) -o $@ $@.c $^ $(LIB)

echoserver: echo.o csapp.o
	$(CC) $(CFLAGS) -o $@ $@.c $^ $(LIB)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c $^
echo.o: echo.c
	$(CC) $(CFLAGS) -c $^
clean:
	rm -f *.o
	rm -f $(TARGET)