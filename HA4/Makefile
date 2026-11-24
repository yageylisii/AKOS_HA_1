CC = cc
CFLAGS ?= -O2 -g

CPPFLAGS += -Iinclude
CFLAGS += -std=gnu11
CFLAGS += -Wall -Wextra -Werror -Wformat-security -Wignored-qualifiers -Winit-self \
	-Wswitch-default -Wpointer-arith -Wtype-limits -Wempty-body \
	-Wstrict-prototypes -Wold-style-definition -Wmissing-field-initializers \
	-Wnested-externs -Wmissing-prototypes -Wfloat-equal

ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
CFLAGS += -Wold-style-declaration -Wmissing-parameter-type
else
CFLAGS += -Wabsolute-value
endif

LDLIBS += -lm

BUILD_DIR = build

# предметная модель из HA3: одинакова для сервера и всех клиентов
MODEL_SOURCES = \
	src/components/model.c \
	src/components/queue.c \
	src/inputs/input_data.c \
	src/inputs/input_logs.c \
	src/net/protocol.c \
	src/net/model_wire.c \
	src/net/tcp.c

# шаги участников нужны клиентам и тестам
ROLE_SOURCES = \
	src/roles/cabin.c \
	src/roles/operator.c \
	src/roles/station.c

MODEL_OBJS = $(addprefix $(BUILD_DIR)/,$(MODEL_SOURCES:.c=.o))
ROLE_OBJS = $(addprefix $(BUILD_DIR)/,$(ROLE_SOURCES:.c=.o))
SERVER_OBJS = $(BUILD_DIR)/src/roles/server.o $(BUILD_DIR)/app/server_main.o
CLIENT_OBJS = $(BUILD_DIR)/src/roles/client.o

TARGETS = cc_server cc_operator cc_station cc_cabin cc_observer
TEST_TARGETS = $(BUILD_DIR)/tests/model_test $(BUILD_DIR)/tests/protocol_test

ALL_OBJS = $(MODEL_OBJS) $(ROLE_OBJS) $(SERVER_OBJS) $(CLIENT_OBJS) \
	$(BUILD_DIR)/app/operator_main.o $(BUILD_DIR)/app/station_main.o \
	$(BUILD_DIR)/app/cabin_main.o $(BUILD_DIR)/app/observer_main.o \
	$(BUILD_DIR)/tests/model_test.o $(BUILD_DIR)/tests/protocol_test.o
DEPS = $(ALL_OBJS:.o=.d)

.PHONY: all run demo demo-reconnect test clean help

all: $(TARGETS)

cc_server: $(MODEL_OBJS) $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

cc_operator: $(MODEL_OBJS) $(ROLE_OBJS) $(CLIENT_OBJS) $(BUILD_DIR)/app/operator_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

cc_station: $(MODEL_OBJS) $(ROLE_OBJS) $(CLIENT_OBJS) $(BUILD_DIR)/app/station_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

cc_cabin: $(MODEL_OBJS) $(ROLE_OBJS) $(CLIENT_OBJS) $(BUILD_DIR)/app/cabin_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

cc_observer: $(MODEL_OBJS) $(ROLE_OBJS) $(CLIENT_OBJS) $(BUILD_DIR)/app/observer_main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/tests/model_test: $(MODEL_OBJS) $(ROLE_OBJS) $(BUILD_DIR)/tests/model_test.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/tests/protocol_test: $(MODEL_OBJS) $(ROLE_OBJS) $(BUILD_DIR)/tests/protocol_test.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# всё на одном компьютере: сервер, участники и наблюдатель
run: $(TARGETS)
	./scripts/run_local.sh

# сценарий на бонус: отключение и переподключение клиентов во время работы
demo: demo-reconnect

demo-reconnect: $(TARGETS)
	./scripts/demo_reconnect.sh

test: $(TARGETS) $(TEST_TARGETS)
	$(BUILD_DIR)/tests/model_test
	$(BUILD_DIR)/tests/protocol_test
	./scripts/test_network.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGETS) logs

help:
	@echo "make                 сборка сервера и четырёх клиентов"
	@echo "make run             локальный запуск: сервер, все участники и наблюдатель"
	@echo "make demo-reconnect  отключение и переподключение кабины и наблюдателей"
	@echo "make test            тесты модели, протокола и сетевые прогоны"

-include $(DEPS)

.PHONY: test-linux test-distributed
# Docker is optional; normal builds and local tests do not depend on it.
test-linux:
	docker build -t akos-ha4-test .
	docker run --rm akos-ha4-test

test-distributed:
	docker build -t akos-ha4-test .
	./scripts/test_distributed.sh

.PHONY: submission
submission:
	python3 scripts/package.py
