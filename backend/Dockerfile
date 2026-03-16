# ============================================================
# ÉTAPE 1 — compilation
# ============================================================
FROM debian:12 AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libmariadb-dev \
    libboost-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copie les headers locaux — plus besoin de cloner depuis GitHub
# Crow et Asio sont déjà dans le repo
COPY crow.h      ./crow.h
COPY crow/       ./crow/
COPY AsioLib/    ./AsioLib/
COPY CMakeLists.txt .
COPY src/        ./src/

# Compilation
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel $(nproc)

# ============================================================
# ÉTAPE 2 — image finale légère
# Repart d'une image propre sans les outils de build
# ============================================================
FROM debian:12-slim

RUN apt-get update && apt-get install -y \
    libmariadb3 \
    libboost-system1.74.0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copie uniquement le binaire depuis l'étape builder
COPY --from=builder /app/build/study-wiki-backend .

EXPOSE 8080

CMD ["./study-wiki-backend"]