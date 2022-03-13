## Server:
```shell
 $ ./ipk-simpleftp-server {-i rozhraní} {-p port} [-u cesta_soubor] [-f cesta_k_adresari]
 $ ./ipk-simpleftp-server -i enp9s0 -p 3490 -u "/home/user/Documents/SKOLA/IPK/FTP/my/userpass.txt" -f "/Server/"

 -u -> absolutna cesta k databaze
 -f -> absolutna alebo lokalna cesta k pracovnemu priecinku (ak je zadana absolutna program ju prevedie na lokalnu)
 -p -> port (porty 0-1023 su na linuxe rezervovane pre system)
 -i -> rozhranie (zobrazenie dostupnych rozhrani: ifconfig  /  ip a)
```

## Klient:
```shell
 $ ./ipk-simpleftp-client [-h IP] {-p port} [-f cesta_k_adresari]
 $ ./ipk-simpleftp-client -h 127.0.0.1 -p 3490 -f "/Client"

 -f -> absolutna alebo lokalna cesta k pracovnemu priecinku (ak je zadana absolutna program ju prevedie na lokalnu)
 -p -> port (porty 0-1023 su na linuxe rezervovane pre system)
 -h -> adresa na ktorej bezi server (ipv4 alebo ipv6)
```

## Preklad
```shell
 $ make
```

## Obmedzenia
> nepodporuje nazvy suborov s medzerami (RETR "test file.txt" **neprejde** RETR test_file.txt **prejde**)

## Zoznam odovzdanych suborov
> ipk-simpleftp-server.c
ipk-simpleftp-client.c
Makefile
README.md
manual.pdf
