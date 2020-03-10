SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

relay: $(OBJ)
	$(CC) -o $@ $^

.PHONY:clean
clean:
	rm -f relay $(OBJ)

.PHONY:test
test: relay
	./relay -c 50 -f 100 -o 11111 &
	./relay -i 11111 -o 11112 &
	./relay -i 11112 -o 11113 &
	./relay -i 11113 -o 11114 &
	./relay -i 11114 -o 11115 &
	./relay -i 11115
