all: vpn_main.c
	gcc -o minivpn vpn_main.c
clean:
	rm  minivpn
