FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates libcurl4-openssl-dev libcjson-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 libcjson1 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app/build/openhab_backend /app/methods.json ./
EXPOSE 8080
ENV PORT=8080
ENTRYPOINT ["./openhab_backend"]
