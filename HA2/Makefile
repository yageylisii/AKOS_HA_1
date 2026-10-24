CC = cc
CFLAGS ?= -O2 -g
CFLAGS += -pthread

CPPFLAGS += -Iinclude
CFLAGS += -std=gnu99
CFLAGS += -Wall -Werror -Wformat-security -Wignored-qualifiers -Winit-self \
	-Wswitch-default -Wpointer-arith -Wtype-limits -Wempty-body \
	-Wstrict-prototypes -Wold-style-definition -Wmissing-field-initializers \
	-Wnested-externs -Wmissing-prototypes -Wfloat-equal

# Эти предупреждения поддерживает GCC, но не Apple Clang.
ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS += -Wold-style-declaration -Wmissing-parameter-type -Wstack-usage=4096
else
CFLAGS += -Wabsolute-value -Wno-deprecated-declarations
endif

SANITIZER_FLAGS = -fsanitize=undefined -fsanitize-undefined-trap-on-error
CFLAGS += $(SANITIZER_FLAGS)
LDFLAGS += $(SANITIZER_FLAGS) -pthread
LDLIBS += -lm

TARGET = cable_car
BUILD_DIR = build

C_SOURCE = app/main.c src/components/service.c src/inputs/input_data.c src/inputs/input_logs.c \
	src/queues/queue_passenger.c src/queues/queue_cabin.c \
	src/components/station_worker.c src/components/cabin_worker.c \
	src/components/passenger_worker.c src/threads/mailbox.c src/threads/runtime.c
OBJS = $(addprefix $(BUILD_DIR)/,$(C_SOURCE:.c=.o))
DEPS = $(OBJS:.o=.d)

.PHONY: all run clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

TEST_TARGET = $(BUILD_DIR)/tests/service_test
TEST_OBJS = $(BUILD_DIR)/tests/service_test.o $(BUILD_DIR)/tests/input_logs.o \
	$(BUILD_DIR)/src/components/service.o $(BUILD_DIR)/src/queues/queue_passenger.o \
	$(BUILD_DIR)/src/queues/queue_cabin.o $(BUILD_DIR)/src/threads/mailbox.o \
	$(BUILD_DIR)/src/threads/runtime.o $(BUILD_DIR)/src/components/station_worker.o \
	$(BUILD_DIR)/src/components/cabin_worker.o $(BUILD_DIR)/src/components/passenger_worker.o

# В тестовой сборке выключается только реальная пауза журнала.
$(BUILD_DIR)/tests/input_logs.o: src/inputs/input_logs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dnanosleep=test_nanosleep -MMD -MP -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TEST_OBJS) $(LDLIBS)

test: $(TEST_TARGET)
	$(TEST_TARGET) > $(BUILD_DIR)/tests/service.log

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(TEST_TARGET) $(TEST_OBJS) $(TEST_OBJS:.o=.d) $(BUILD_DIR)/tests/service.log

-include $(DEPS) $(TEST_OBJS:.o=.d)
