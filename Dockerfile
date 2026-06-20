FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libcurl4-openssl-dev \
    libssl-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN git submodule update --init --recursive || true

RUN mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && cmake --build . -- -j$(nproc)

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y \
    libcurl4 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/openhab_backend .
EXPOSE 8080
ENV PORT=8080
ENTRYPOINT ["./openhab_backend"]
