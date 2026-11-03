CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all test clean
all: batcher

batcher: batcher.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LDFLAGS) -o $@ $< $(LDLIBS)

test: batcher
	@set -e; count=0; \
	for n in 0 1 2 3 4 5 7 8 9 15 16 17 31 32 33 63 64 65 127 128 129 1023 1024 1025 4097 65536 65537; do \
		for p in 1 2 3 4 7; do \
			for seed in 42 2026; do \
				./batcher $$n $$p $$seed >/dev/null; count=$$((count + 1)); \
			done; \
		done; \
	done; \
	printf '%s cases passed\n' "$$count"

clean:
	$(RM) batcher
