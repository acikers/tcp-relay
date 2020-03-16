SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

CFLAGS=-O3

FREQ?=100
COUNT?=3000
NOBLOCK?=0
NODELAY?=0
MSGLEN?=1024

DEFINES+= -DNOBLOCK=$(NOBLOCK)
DEFINES+= -DNODELAY=$(NODELAY)
DEFINES+= -DMSGLEN=$(MSGLEN)
ifdef SCHEDCPU
DEFINES+= -DSCHEDCPU=\"$(SCHEDCPU)\"
endif

override OUTPUT=test.csv


.PHONY:all
all: plot

%.o:CFLAGS+=$(DEFINES)

ifdef SCHEDFIFO
PREFIX_CMD1=chrt -f 99
PREFIX_CMD2=chrt -f 98
PREFIX_CMD3=chrt -f 97
PREFIX_CMD4=chrt -f 96
PREFIX_CMD5=chrt -f 95
PREFIX_CMD6=chrt -f 94
endif

#PREFIX_CMD1=nice -n -20
#PREFIX_CMD=$(PREFIX_CMD)

relay: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

.PHONY:clean
clean:
	rm -f relay $(OBJ) plot.pdf $(OUTPUT)

.PHONY:test
test: $(OUTPUT)
$(OUTPUT): relay
	$(PREFIX_CMD1) ./relay -c $(COUNT) -f $(FREQ) -o 11111 &
	$(PREFIX_CMD2) ./relay -i 11111 -o 11112 &
	$(PREFIX_CMD3) ./relay -i 11112 -o 11113 &
	$(PREFIX_CMD4) ./relay -i 11113 -o 11114 &
	$(PREFIX_CMD5) ./relay -i 11114 -o 11115 &
	$(PREFIX_CMD6) ./relay -i 11115 -r $(OUTPUT)

.PHONY:plot
plot: plot.png
plot.png: $(OUTPUT) plot.py
	./plot.py $(OUTPUT) $@
