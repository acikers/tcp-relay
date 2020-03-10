SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

relay: $(OBJ)
	$(CC) -o $@ $^

.PHONY:clean
clean:
	rm -f relay $(OBJ)

.PHONY:test
test: relay
	./relay -c 50 -f 100 -o 55551 &
	./relay -i 55551 -o 55552 &
	./relay -i 55552 -o 55553 &
	./relay -i 55553 -o 55554 &
	./relay -i 55554 -o 55555 &
	./relay -i 55555
