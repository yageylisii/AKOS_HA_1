CC = cc
CFLAGS ?= -O2 -g

CPPFLAGS += -Iinclude
CFLAGS += -std=gnu11
CFLAGS += -Wall -Wextra -Werror -Wformat-security -Wignored-qualifiers -Winit-self \
	-Wswitch-default -Wpointer-arith -Wtype-limits -Wempty-body \
	-Wstrict-prototypes -Wold-style-definition -Wmissing-field-initializers \
	-Wnested-externs -Wmissing-prototypes -Wfloat-equal

# эти предупреждения поддерживает GCC, но не Apple Clang
ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS += -Wold-style-declaration -Wmissing-parameter-type
else
CFLAGS += -Wabsolute-value
endif

LDLIBS += -lm -pthread
# разделяемая память POSIX в glibc живёт в librt, в macOS она в libc
ifeq ($(shell uname -s),Linux)
LDLIBS += -lrt
endif

BUILD_DIR = build

# общее ядро модели: одинаково для всех трёх вариантов программы
CORE_SOURCES = \
	src/components/model.c \
	src/components/queue.c \
	src/inputs/input_data.c \
	src/inputs/input_logs.c \
	src/ipc/shared_memory.c \
	src/ipc/sync.c \
	src/roles/cabin.c \
	src/roles/clock.c \
	src/roles/operator.c \
	src/roles/station.c \
	src/roles/worker.c

FORKED_SOURCES = src/roles/launcher.c
STANDALONE_SOURCES = src/roles/standalone.c

CORE_OBJS = $(addprefix $(BUILD_DIR)/,$(CORE_SOURCES:.c=.o))
FORKED_OBJS = $(addprefix $(BUILD_DIR)/,$(FORKED_SOURCES:.c=.o))
STANDALONE_OBJS = $(addprefix $(BUILD_DIR)/,$(STANDALONE_SOURCES:.c=.o))

# 4-5 баллов: родитель и потомки, неименованные семафоры
TARGET_UNNAMED = cable_car_unnamed
# 6 баллов: родитель и потомки, именованные семафоры
TARGET_NAMED = cable_car_named
# 7-8 баллов: независимо запускаемые программы-процессы
TARGET_CONTROL = cc_control
TARGET_OPERATOR = cc_operator
TARGET_CABIN = cc_cabin
TARGET_STATION = cc_station

TARGETS = $(TARGET_UNNAMED) $(TARGET_NAMED) $(TARGET_CONTROL) \
	$(TARGET_OPERATOR) $(TARGET_CABIN) $(TARGET_STATION)

TEST_TARGET = $(BUILD_DIR)/tests/model_test

ALL_OBJS = $(CORE_OBJS) $(FORKED_OBJS) $(STANDALONE_OBJS) \
	$(BUILD_DIR)/app/forked_unnamed/main.o \
	$(BUILD_DIR)/app/forked_named/main.o \
	$(BUILD_DIR)/app/standalone/control.o \
	$(BUILD_DIR)/app/standalone/operator_main.o \
	$(BUILD_DIR)/app/standalone/cabin_main.o \
	$(BUILD_DIR)/app/standalone/station_main.o \
	$(BUILD_DIR)/tests/model_test.o
DEPS = $(ALL_OBJS:.o=.d)

.PHONY: all run run-unnamed run-named run-standalone test clean cleanipc help

all: $(TARGETS)

$(TARGET_UNNAMED): $(CORE_OBJS) $(FORKED_OBJS) $(BUILD_DIR)/app/forked_unnamed/main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TARGET_NAMED): $(CORE_OBJS) $(FORKED_OBJS) $(BUILD_DIR)/app/forked_named/main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TARGET_CONTROL): $(CORE_OBJS) $(BUILD_DIR)/app/standalone/control.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TARGET_OPERATOR): $(CORE_OBJS) $(STANDALONE_OBJS) $(BUILD_DIR)/app/standalone/operator_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TARGET_CABIN): $(CORE_OBJS) $(STANDALONE_OBJS) $(BUILD_DIR)/app/standalone/cabin_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TARGET_STATION): $(CORE_OBJS) $(STANDALONE_OBJS) $(BUILD_DIR)/app/standalone/station_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST_TARGET): $(CORE_OBJS) $(FORKED_OBJS) $(BUILD_DIR)/tests/model_test.o
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# прогон обоих вариантов с общим родительским процессом
run: run-unnamed

run-unnamed: $(TARGET_UNNAMED)
	./$(TARGET_UNNAMED) --config scripts/settings.txt --log log_unnamed.txt

run-named: $(TARGET_NAMED)
	./$(TARGET_NAMED) --config scripts/settings.txt --log log_named.txt

# вариант с независимыми программами: участники запускаются скриптом
run-standalone: $(TARGET_CONTROL) $(TARGET_OPERATOR) $(TARGET_CABIN) $(TARGET_STATION)
	./scripts/run_standalone.sh

test: $(TEST_TARGET)
	$(TEST_TARGET)

# удаление остатков разделяемой памяти и семафоров после аварийного завершения
cleanipc:
	-@rm -f /dev/shm/akos46_* /dev/shm/sem.akos46_* 2>/dev/null || true
	@echo "Остатки ресурсов POSIX удалены"

clean:
	rm -rf $(BUILD_DIR) $(TARGETS) log.txt log_unnamed.txt log_named.txt log_standalone.txt

help:
	@echo "make               сборка всех шести программ"
	@echo "make run-unnamed    4-5 баллов: неименованные семафоры"
	@echo "make run-named      6 баллов: именованные семафоры"
	@echo "make run-standalone 7-8 баллов: независимые программы"
	@echo "make test           исходные тесты модели"
	@echo "make cleanipc       удалить остатки семафоров и разделяемой памяти"

-include $(DEPS)
