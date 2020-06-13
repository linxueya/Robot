src = $(wildcard *.c)
obj = $(patsubst %.c, %.o, $(src))

all: server client

server: server.o wrap.o link.o
	gcc server.o wrap.o link.o -o server -Wall -lpthread
client: client.o wrap.o
	gcc client.o wrap.o -o client -Wall -lpthread

%.o:%.c
	gcc -c $< -Wall 

.PHONY: clean all
clean: 
	-rm -rf server client $(obj)



