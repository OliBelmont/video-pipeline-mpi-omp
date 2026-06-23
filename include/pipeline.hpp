#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "filters.hpp" // Necessário porque AppConfig guarda o tipo de filtro selecionado.
#include <string>       // Necessário para caminhos de entrada/saída com std::string.

// Tags MPI usadas para identificar o tipo de mensagem enviada entre coordenador e workers.
// O uso de tags deixa claro se a mensagem é metadado, bytes do frame, resultado ou parada.
constexpr int TAG_FRAME_META = 10;   // Metadados do frame enviado pelo coordenador: id, linhas, colunas, tipo e filtro.
constexpr int TAG_FRAME_DATA = 11;   // Bytes do frame original enviados para o worker.
constexpr int TAG_RESULT_META = 20;  // Metadados do frame processado devolvido pelo worker.
constexpr int TAG_RESULT_DATA = 21;  // Bytes do frame processado devolvido pelo worker.
constexpr int TAG_STOP = 99;         // Sinal enviado pelo coordenador para encerrar os workers.

// Estrutura com as configurações de execução do programa.
// Ela é preenchida em main.cpp a partir dos argumentos passados no terminal.
struct AppConfig {
    bool use_webcam = false;                    // true = usa webcam; false = usa arquivo.
    int webcam_index = 0;                       // Índice da webcam. Normalmente 0 para a webcam principal.
    std::string input_path;                     // Caminho do arquivo .mp4 ou .avi quando a entrada é por arquivo.
    std::string output_path = "output.avi";     // Nome do vídeo gerado pelo programa.
    FilterType filter = FilterType::GRAYSCALE;  // Filtro padrão caso o usuário não informe outro.
    int max_frames = 0;                         // 0 = processa todos os frames disponíveis.
    int threads = 0;                            // 0 = usa a quantidade padrão definida pelo OpenMP/ambiente.
    bool display = false;                     
};

// Função executada pelo rank 0. Lê frames, distribui para workers e grava o resultado.
int run_coordinator(const AppConfig& config, int world_size);

// Função executada pelos ranks 1, 2, 3... Recebe frame, processa e devolve ao coordenador.
int run_worker(int rank);

// Função de ajuda implementada em main.cpp.
void print_usage(const char* executable_name);

#endif
