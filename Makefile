APITRACE=apitrace
QAPITRACE=qapitrace
CC=gcc

CC_OPTS=-Wall -std=c99 -pedantic -O2
LIBS=-lGL -lSDL2

PROG=multiview
SRC=$(PROG).c

$(PROG): $(SRC)
	$(CC) $(CC_OPTS) $(LIBS) -o $@ $^ 

.PHONY: trace
trace: $(PROG)
	rm -f $(PROG).trace
	$(APITRACE) trace ./$(PROG)
	$(QAPITRACE) $(PROG).trace

.PHONY: clean
clean:
	rm -f $(PROG) $(PROG).trace
