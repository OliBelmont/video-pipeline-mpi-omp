#!/usr/bin/env bash

# Script de execução usando webcam.
# Ele serve para facilitar o teste sem precisar digitar o comando mpirun completo.
set -euo pipefail

# Primeiro parâmetro: filtro desejado.
FILTER="${1:-denoise}"

# Segundo parâmetro: quantidade de processos MPI.
MPI_PROCS="${2:-4}"

# Terceiro parâmetro: quantidade de threads OpenMP por worker.
OMP_THREADS="${3:-4}"

# Nome do arquivo de saída.
OUTPUT="output_webcam_${FILTER}.avi"

# Compila antes de executar.
make

# Executa usando a webcam 0 como entrada.
mpirun -np "${MPI_PROCS}" ./bin/video_pipeline \
  --webcam 0 \
  --filter "${FILTER}" \
  --output "${OUTPUT}" \
  --threads "${OMP_THREADS}" \
  --max-frames 120
