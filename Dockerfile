# ── Stage 1: build ───────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake make libncurses-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# ── Stage 2: server runtime ───────────────────────────────────────────────────
FROM debian:bookworm-slim AS monitor-server

RUN apt-get update && apt-get install -y --no-install-recommends \
    libncurses6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/monitor
COPY --from=builder /src/monitor_server    ./monitor_server
COPY --from=builder /src/config/           ./config/
RUN mkdir -p data/history

EXPOSE 8784 8785

ENTRYPOINT ["./monitor_server"]
CMD ["-port", "8784", "-vport", "8785", \
     "-config", "config/thresholds.conf", \
     "-server-config", "config/server.conf"]

# ── Stage 3: agent runtime ────────────────────────────────────────────────────
FROM debian:bookworm-slim AS agent

WORKDIR /opt/monitor
COPY --from=builder /src/agent             ./agent
COPY --from=builder /src/config/agent.conf ./config/

# Required env vars:  MONITOR_SERVER (host:port)  AGENT_NAME
ENV MONITOR_SERVER=monitor-server:8784
ENV AGENT_NAME=""
ENV AGENT_INTERVAL=3

ENTRYPOINT ["sh", "-c", \
  "exec ./agent -fg -server ${MONITOR_SERVER} -name ${AGENT_NAME:-$(hostname)} \
   -interval ${AGENT_INTERVAL} -config config/agent.conf"]
