CFLAGS= -I. -g -O0 -Wall -DEV_EPOLL -Wl,-rpath=/lib -Wl,-rpath=/usr/local/lib
all: ev
ev: ev.o test_ev.o log.o
	gcc $(CFLAGS) -o $@ $^ -lpthread
clean:	
	-rm -f ev *.o	
.PHONY: all clean
