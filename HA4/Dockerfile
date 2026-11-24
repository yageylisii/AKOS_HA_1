FROM python:3.12-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work/HA4
COPY . .
RUN make all build/tests/model_test build/tests/protocol_test
CMD ["make", "test"]
