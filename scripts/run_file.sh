#!/usr/bin/env bash

# Encerra o script se algum comando falhar, se usar variável não declarada
# ou se algum comando dentro de pipeline retornar erro.
set -euo pipefail

# Primeiro parâmetro: caminho do vídeo de entrada.
# Se o usuário não informar, usa examples/video_teste.mp4.
VIDEO_PATH="${1:-examples/video_teste.mp4}"

# Segundo parâmetro: filtro usado no processamento.
# Opções: grayscale, sharpen ou denoise.
FILTER="${2:-sharpen}"

# Terceiro parâmetro: quantidade de processos MPI.
# Com 4 processos, normalmente fica 1 coordenador e 3 workers.
MPI_PROCS="${3:-4}"

# Quarto parâmetro: quantidade de threads OpenMP por worker.
OMP_THREADS="${4:-4}"

# Nome do arquivo de saída gerado automaticamente conforme o filtro.
OUTPUT="output_${FILTER}.avi"

# Compila o projeto antes de executar.
make

# Executa o pipeline usando arquivo de vídeo como entrada.
mpirun -np "${MPI_PROCS}" ./bin/video_pipeline \
  --input "${VIDEO_PATH}" \
  --filter "${FILTER}" \
  --output "${OUTPUT}" \
  --threads "${OMP_THREADS}" \
  --max-frames 120
