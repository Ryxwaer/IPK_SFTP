all: server client

server: ipk-simpleftp-server.c
	gcc ipk-simpleftp-server.c -o ipk-simpleftp-server

client: ipk-simpleftp-client.c
	gcc ipk-simpleftp-client.c -o ipk-simpleftp-client
