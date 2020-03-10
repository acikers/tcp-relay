SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

relay: $(OBJ)
	$(CC) -o $@ $^

.PHONY:clean_socks
clean_socks:
	rm -f sock1 sock2 sock3 sock4

.PHONY:clean
clean:
	rm -f relay $(OBJ)

.PHONY:test
test: relay clean_socks
	./relay -c 5 -o sock1 &
	./relay -i sock1 -o sock2 &
	./relay -i sock2 -o sock3 &
	./relay -i sock3 -o sock4 &
	./relay -i sock4 &
