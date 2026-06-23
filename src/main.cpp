#include "pipeline.hpp"      // Declara AppConfig, run_coordinator, run_worker e constantes do projeto.
#include <mpi.h>              // Biblioteca MPI: inicialização, rank, quantidade de processos etc.
#include <cstdlib>            // Usado para std::exit e setenv.
#include <cctype>             // Usado para std::isdigit na validação de números.
#include <exception>          // Usado para capturar erros com std::exception.
#include <iostream>           // Usado para imprimir mensagens no terminal.
#include <string>             // Usado para manipular argumentos como std::string.

// Mostra no terminal como o programa deve ser executado.
// Esta função é chamada quando o usuário usa --help ou quando ocorre erro nos argumentos.
void print_usage(const char* executable_name) {
    std::cout << "Uso:\n"
              << "  mpirun -np <processos> " << executable_name << " [opções]\n\n"
              << "Opções principais:\n"
              << "  --input <arquivo.mp4|arquivo.avi>    Usa arquivo de vídeo como entrada\n"
              << "  --webcam [indice]                    Usa webcam como entrada. Índice padrão: 0\n"
              << "  --filter <grayscale|sharpen|denoise> Filtro manual a ser aplicado\n"
              << "  --output <arquivo.avi>               Arquivo de saída. Padrão: output.avi\n"
              << "  --max-frames <n>                     Limita a quantidade de frames. 0 = todos\n"
              << "  --threads <n>                        Quantidade de threads OpenMP por worker\n"
              << "  --display <0|1>                      Exibe janela com frames. Padrão: 1\n"
              << "  --help                               Mostra esta ajuda\n\n"
              << "Exemplo com arquivo:\n"
              << "  mpirun -np 4 " << executable_name
              << " --input examples/video_teste.mp4 --filter sharpen --output saida.avi --threads 4 --max-frames 120\n\n"
              << "Exemplo com webcam:\n"
              << "  mpirun -np 4 " << executable_name
              << " --webcam 0 --filter denoise --output webcam_saida.avi --threads 4\n";
}

// Namespace anônimo: as funções abaixo ficam visíveis apenas neste arquivo.
// Isso evita conflito de nomes com outros arquivos do projeto.
namespace {

// Verifica se uma string contém somente dígitos.
// É usado para saber se o parâmetro depois de --webcam é um índice numérico.
bool is_number(const std::string& value) {
    if (value.empty()) return false; // String vazia não é número.

    for (char c : value) { // Percorre cada caractere informado.
        if (!std::isdigit(static_cast<unsigned char>(c))) return false; // Se algum caractere não for dígito, retorna falso.
    }

    return true; // Se passou por todos os caracteres, a string é numérica.
}

// Lê os argumentos digitados no terminal e monta a configuração do programa.
// Exemplo de argumento: --filter sharpen, --threads 4, --input video.mp4.
AppConfig parse_args(int argc, char** argv) {
    AppConfig config; // Começa com valores padrão definidos em pipeline.hpp.

    for (int i = 1; i < argc; ++i) { // Começa em 1 porque argv[0] é o nome do executável.
        const std::string arg = argv[i]; // Converte o argumento atual para std::string.

        if (arg == "--help" || arg == "-h") { // Ajuda do programa.
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--input" && i + 1 < argc) { // Entrada por arquivo.
            config.input_path = argv[++i]; // Avança para o próximo argumento e guarda o caminho do vídeo.
            config.use_webcam = false;     // Garante que a webcam não será usada.
        } else if (arg == "--webcam") { // Entrada pela webcam.
            config.use_webcam = true;
            if (i + 1 < argc && is_number(argv[i + 1])) { // Permite informar o índice da webcam, como --webcam 0.
                config.webcam_index = std::stoi(argv[++i]);
            }
        } else if (arg == "--filter" && i + 1 < argc) { // Filtro escolhido.
            config.filter = filter_from_string(argv[++i]); // Converte texto para o enum FilterType.
        } else if (arg == "--output" && i + 1 < argc) { // Arquivo de saída.
            config.output_path = argv[++i];
        } else if (arg == "--max-frames" && i + 1 < argc) { // Limite de frames.
            config.max_frames = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) { // Quantidade de threads OpenMP.
            config.threads = std::stoi(argv[++i]);
            if (config.threads > 0) {
                // Define também a variável de ambiente usada pelo OpenMP.
                // O terceiro parâmetro 1 indica que o valor pode sobrescrever outro existente.
                setenv("OMP_NUM_THREADS", std::to_string(config.threads).c_str(), 1);
            }
        } else if (arg == "--display" && i + 1 < argc) { // Controla se abre janela do OpenCV.
            config.display = std::stoi(argv[++i]) != 0;
        } else {
            throw std::invalid_argument("Argumento inválido ou incompleto: " + arg);
        }
    }

    if (!config.use_webcam && config.input_path.empty()) {
        // Caso o usuário não informe vídeo nem webcam, usa webcam 0 como padrão.
        // Isso facilita testes rápidos na apresentação.
        config.use_webcam = true;
        config.webcam_index = 0;
    }

    return config; // Devolve a configuração pronta para o main.
}

} // fim do namespace anônimo

int main(int argc, char** argv) {
    // Inicializa o ambiente MPI. Depois desta chamada cada processo sabe que faz parte do mesmo comunicador.
    MPI_Init(&argc, &argv);

    int rank = 0;       // Identificador do processo atual dentro do MPI.
    int world_size = 1; // Quantidade total de processos MPI iniciados pelo mpirun.

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);      // Descobre o rank deste processo.
    MPI_Comm_size(MPI_COMM_WORLD, &world_size); // Descobre quantos processos existem no total.

    int exit_code = 0; // Código final do programa. 0 significa sucesso.

    try {
        AppConfig config = parse_args(argc, argv); // Lê os parâmetros da linha de comando.

        if (rank == 0) { // Por definição do projeto, o rank 0 será o coordenador.
                std::cout << "=== Processamento de vídeo com MPI + OpenMP ===\n"
                    << "Processos MPI: " << world_size << " (1 coordenador + " << (world_size - 1) << " workers)\n"
                    << "Filtro: " << filter_to_string(config.filter) << "\n"
                    << "Saída: " << config.output_path << "\n"
                    << "----------------------------------------------" << std::endl;

            exit_code = run_coordinator(config, world_size); // Inicia a lógica do coordenador.
        } else { // Qualquer rank maior que 0 será worker.
            exit_code = run_worker(rank); // Inicia a lógica do trabalhador.
        }
    } catch (const std::exception& ex) { // Captura erros de argumento, vídeo, OpenCV etc.
        std::cerr << "[Rank " << rank << "] Erro: " << ex.what() << std::endl;

        if (rank == 0) { // Só o coordenador imprime o uso para não poluir o terminal com todos os ranks.
            print_usage(argv[0]);
        }

        exit_code = 1; // Sinaliza erro.
    }

    MPI_Finalize(); // Finaliza o MPI corretamente antes de encerrar o processo.
    return exit_code;
}
