SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

relay: $(OBJ)
	$(CC) -o $@ $^

.PHONY:clean
clean:
	rm -f relay $(OBJ)

.PHONY:test
test: relay
	./relay -o sock1
