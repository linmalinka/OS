CC = gcc

CFLAGS = -Wall -Wextra -Wpedantic -Werror -std=c11 -O2 -D_FORTIFY_SOURCE=2
CPPFLAGS = -I./include

PARENT_TARGET = main.out
CHILD_TARGET = child

BUILD_DIR = build

all: $(PARENT_TARGET) $(CHILD_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PARENT_TARGET): $(BUILD_DIR)/parent.o
	$(CC) $(CFLAGS) $^ -o $@

$(CHILD_TARGET): $(BUILD_DIR)/child.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/parent.o: parent.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/child.o: child.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(PARENT_TARGET) $(CHILD_TARGET) $(BUILD_DIR)

.PHONY: all clean