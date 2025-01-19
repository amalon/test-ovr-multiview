APITRACE=apitrace
QAPITRACE=qapitrace
CC=gcc

CC_OPTS=-Wall -std=c99 -pedantic -O2
LIBS=-lGL -lSDL2

PROGS+=multiview

SRCS=$(PROGS:=.c)
TRACE_FILES=$(PROGS:=.trace)

all: $(PROGS)

$(PROGS): %: %.c
	$(CC) $(CC_OPTS) $(LIBS) -o $@ $^

TRACE_TARGETS=$(PROGS:%=trace_%)
.PHONY: $(TRACE_TARGETS)
$(TRACE_TARGETS): trace_%: %
	rm -f $<.trace
	$(APITRACE) trace ./$<
	$(QAPITRACE) $<.trace

.PHONY: clean
clean:
	rm -f $(PROGS) $(TRACE_FILES)
