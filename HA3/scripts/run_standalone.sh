#!/usr/bin/env bash
# запуск варианта с независимыми программами-процессами (7-8 баллов)
#
# программы можно запускать и вручную из разных терминалов:
#   ./cc_control --config scripts/settings.txt
#   ./cc_operator
#   ./cc_station A
#   ./cc_station B
#   ./cc_cabin        (столько раз, сколько кабин задано настройками)
#
# этот скрипт делает то же самое автоматически: журнал модели выводит
# управляющая программа, служебные сообщения участников уходят в файл

set -u

CONFIG="${1:-scripts/settings.txt}"
DELAY="${2:-150}"
WORKER_LOG="workers.log"

if [ ! -f "$CONFIG" ]; then
    echo "Не найден конфигурационный файл: $CONFIG" >&2
    exit 1
fi

for binary in ./cc_control ./cc_operator ./cc_station ./cc_cabin; do
    if [ ! -x "$binary" ]; then
        echo "Не собрана программа $binary, выполните make" >&2
        exit 1
    fi
done

# первое значащее число конфигурации — количество кабин
CABINS=$(grep -v '^[[:space:]]*#' "$CONFIG" | grep -v '^[[:space:]]*$' | head -1 | awk '{print $1}')
if ! [ "$CABINS" -ge 1 ] 2>/dev/null; then
    echo "Не удалось определить количество кабин из $CONFIG" >&2
    exit 1
fi

PIDS=()

cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT INT TERM

: > "$WORKER_LOG"

echo "Запуск управляющей программы, кабин: $CABINS" >&2
./cc_control --config "$CONFIG" --log log_standalone.txt --delay "$DELAY" &
CONTROL_PID=$!
PIDS+=("$CONTROL_PID")

# даём управляющей программе создать разделяемую память и семафоры
sleep 1

./cc_operator >>"$WORKER_LOG" 2>&1 &
PIDS+=("$!")

./cc_station A >>"$WORKER_LOG" 2>&1 &
PIDS+=("$!")

./cc_station B >>"$WORKER_LOG" 2>&1 &
PIDS+=("$!")

for _ in $(seq 1 "$CABINS"); do
    ./cc_cabin >>"$WORKER_LOG" 2>&1 &
    PIDS+=("$!")
done

wait "$CONTROL_PID"
STATUS=$?

# ждём завершения участников после остановки модели
sleep 1

echo "Служебные сообщения участников: $WORKER_LOG" >&2
exit "$STATUS"
