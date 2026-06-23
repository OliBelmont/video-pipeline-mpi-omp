# Makefile do projeto de Programação Paralela e Distribuída
# Objetivo: compilar e executar o sistema de processamento de vídeo com MPI + OpenMP + OpenCV.

# Compilador MPI para C++.
# Usamos mpicxx porque ele já inclui as bibliotecas e flags necessárias do MPI.
CXX := mpicxx

# Flags de compilação:
# -O2       -> otimização intermediária, sem deixar a compilação muito pesada.
# -Wall     -> mostra avisos importantes do compilador.
# -Wextra   -> mostra avisos adicionais.
# -std=c++17 -> define o padrão C++ usado no projeto.
# -fopenmp  -> ativa suporte ao OpenMP.
# -Iinclude -> informa onde estão os headers .hpp do projeto.
CXXFLAGS := -O2 -Wall -Wextra -std=c++17 -fopenmp -Iinclude

# Busca automaticamente as flags do OpenCV pelo pkg-config.
# Se o OpenCV estiver instalado corretamente, isso retorna includes e libs necessárias.
OPENCV := $(shell pkg-config --cflags --libs opencv4 2>/dev/null)

# Arquivos-fonte do projeto.
SRC := src/main.cpp src/coordinator.cpp src/worker.cpp src/filters.cpp

# Pasta onde o executável será gerado.
BIN_DIR := bin

# Caminho final do executável.
TARGET := $(BIN_DIR)/video_pipeline

# Alvos que não representam arquivos reais.
.PHONY: all clean run-file run-webcam check-opencv

# make ou make all:
# verifica OpenCV e compila o executável.
all: check-opencv $(TARGET)

# Verifica se o pkg-config consegue encontrar o OpenCV.
# Se não encontrar, mostra o comando de instalação usado em Ubuntu/Debian.
check-opencv:
	@if [ -z "$(OPENCV)" ]; then \
		echo "Erro: OpenCV não encontrado via pkg-config opencv4."; \
		echo "No Ubuntu/Debian, instale com:"; \
		echo "  sudo apt update && sudo apt install -y build-essential openmpi-bin libopenmpi-dev libopencv-dev pkg-config"; \
		exit 1; \
	fi

# Regra de compilação do executável.
# Cria a pasta bin e compila todos os .cpp de uma vez.
$(TARGET): $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(OPENCV)

# make run-file:
# compila e executa um teste usando o vídeo examples/video_teste.mp4.
# É necessário colocar um vídeo real nesse caminho antes de executar.
run-file: all
	mpirun -np 4 ./$(TARGET) --input examples/video_teste.mp4 --filter sharpen --output output_sharpen.avi --threads 4 --max-frames 120

# make run-webcam:
# compila e executa usando a webcam de índice 0.
run-webcam: all
	mpirun -np 4 ./$(TARGET) --webcam 0 --filter denoise --output output_webcam.avi --threads 4 --max-frames 120

# make clean:
# remove executável e vídeos de saída gerados nos testes.
clean:
	rm -rf $(BIN_DIR) output*.avi *.avi
