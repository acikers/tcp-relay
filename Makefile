#include gmsl

SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)
CFLAGS=-O0

FREQ=100
#TIME=30
#COUNT=$(call multiply, $(FREQ), $(TIME))
COUNT=3000
OUTPUT=test.csv

relay: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY:clean
clean:
	rm -f relay $(OBJ) plot.pdf $(OUTPUT)

.PHONY:test
test: $(OUTPUT)
$(OUTPUT): relay
	./relay -c $(COUNT) -f $(FREQ) -o 11111 &
	./relay -i 11111 -o 11112 &
	./relay -i 11112 -o 11113 &
	./relay -i 11113 -o 11114 &
	./relay -i 11114 -o 11115 &
	./relay -i 11115 -r $(OUTPUT)

.PHONY:plot
plot: plot.pdf
plot.pdf: $(OUTPUT) plot.py
	./plot.py $(OUTPUT) $@
