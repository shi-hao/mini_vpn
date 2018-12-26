all: server.c
	gcc -o minivpn server.c
clean:
	rm  minivpn
