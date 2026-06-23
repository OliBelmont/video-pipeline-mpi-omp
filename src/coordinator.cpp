#include "pipeline.hpp"       // Configurações, tags MPI e filtros usados no pipeline.
#include <mpi.h>               // Envio e recebimento de mensagens entre processos.
#include <opencv2/opencv.hpp>  // Leitura e gravação de vídeo.
#include <chrono>              // Medição de tempo de execução.
#include <iostream>            // Logs no terminal.
#include <map>                 // Buffer para reordenar frames pelo id.
#include <queue>               // Fila de workers disponíveis.
#include <stdexcept>           // Exceções em caso de erro de vídeo ou saída.
#include <vector>              // Buffer de bytes recebido via MPI.

#ifdef _OPENMP
#include <omp.h>               // Permite definir número de threads no processo coordenador/local.
#endif

namespace {

using Clock = std::chrono::high_resolution_clock;

// Calcula a quantidade de bytes de um frame a partir das dimensões e do tipo OpenCV.
size_t frame_bytes(int rows, int cols, int type) {
    return static_cast<size_t>(rows) * static_cast<size_t>(cols) * CV_ELEM_SIZE(type);
}

// Retorna o tempo entre dois pontos em milissegundos.
double elapsed_ms(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Envia um frame do coordenador para um worker específico.
void send_frame_to_worker(int worker_rank, int frame_id, const cv::Mat& frame, FilterType filter) {
    // O MPI precisa enviar uma região contínua de memória.
    // Caso o cv::Mat não esteja contínuo, fazemos uma cópia.
    cv::Mat contiguous = frame.isContinuous() ? frame : frame.clone();

    // Metadados enviados antes dos pixels.
    // O worker usa essas informações para reconstruir o cv::Mat.
    const int meta[5] = {
        frame_id,
        contiguous.rows,
        contiguous.cols,
        contiguous.type(),
        static_cast<int>(filter)
    };

    const int bytes = static_cast<int>(contiguous.total() * contiguous.elemSize());

    // Envia metadados do frame.
    MPI_Send(meta, 5, MPI_INT, worker_rank, TAG_FRAME_META, MPI_COMM_WORLD);

    // Envia os pixels do frame.
    MPI_Send(contiguous.data, bytes, MPI_UNSIGNED_CHAR, worker_rank, TAG_FRAME_DATA, MPI_COMM_WORLD);
}

// Recebe de qualquer worker um frame já processado.
cv::Mat receive_processed_frame(int& source_worker, int& frame_id) {
    MPI_Status status{};
    int meta[4] = {0, 0, 0, 0};

    // Recebe os metadados do primeiro worker que terminar.
    MPI_Recv(meta, 4, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT_META, MPI_COMM_WORLD, &status);

    source_worker = status.MPI_SOURCE;
    frame_id = meta[0];

    const int rows = meta[1];
    const int cols = meta[2];
    const int type = meta[3];
    const int bytes = static_cast<int>(frame_bytes(rows, cols, type));

    std::vector<unsigned char> buffer(bytes);

    // Recebe os pixels processados.
    MPI_Recv(buffer.data(), bytes, MPI_UNSIGNED_CHAR, source_worker, TAG_RESULT_DATA, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Reconstrói o frame a partir do buffer recebido.
    // O clone() garante que o cv::Mat tenha memória própria.
    cv::Mat frame(rows, cols, type, buffer.data());
    return frame.clone();
}

// Verifica se o limite de frames informado pelo usuário já foi atingido.
bool should_stop_by_limit(const AppConfig& config, int next_frame_id) {
    return config.max_frames > 0 && next_frame_id >= config.max_frames;
}

// Envia uma mensagem de parada para todos os workers.
void stop_workers(int world_size) {
    for (int worker = 1; worker < world_size; ++worker) {
        MPI_Send(nullptr, 0, MPI_INT, worker, TAG_STOP, MPI_COMM_WORLD);
    }
}

// Grava todos os frames que já estão prontos e na ordem correta.
// O map funciona como buffer de reordenação, pois os workers podem devolver frames fora de ordem.
void write_ready_frames(std::map<int, cv::Mat>& reorder_buffer,
                        int& next_frame_to_write,
                        cv::VideoWriter& writer,
                        bool display) {
    // A exibição em janela foi desativada para evitar conflito gráfico em alguns ambientes Linux/VS Code/Snap.
    // O sistema segue gravando normalmente o vídeo processado no arquivo de saída.
    (void)display;

    while (reorder_buffer.count(next_frame_to_write) > 0) {
        const cv::Mat frame = reorder_buffer[next_frame_to_write];

        writer.write(frame);

        reorder_buffer.erase(next_frame_to_write);
        ++next_frame_to_write;
    }
}

// Modo local usado quando o programa é executado com apenas 1 processo MPI.
// Não demonstra distribuição real, mas permite testar os filtros com OpenMP.
int run_local_mode(const AppConfig& config, cv::VideoCapture& capture, cv::VideoWriter& writer) {
    std::cout << "Modo local: apenas 1 processo MPI detectado." << std::endl;
    std::cout << "Processando frames localmente com OpenMP..." << std::endl;

    int processed = 0;
    cv::Mat frame;

    const auto start_total = Clock::now();

    while (!should_stop_by_limit(config, processed) && capture.read(frame)) {
        cv::Mat output = apply_filter(frame, config.filter);
        writer.write(output);
        ++processed;
    }

    const auto end_total = Clock::now();

    std::cout << "----------------------------------------------\n"
              << "Execução finalizada com sucesso.\n"
              << "Frames processados: " << processed << "\n"
              << "Tempo total: " << elapsed_ms(start_total, end_total) << " ms\n"
              << "Arquivo gerado: " << config.output_path << std::endl;

    return 0;
}

} // fim do namespace anônimo

// Função principal do coordenador, executada apenas pelo rank 0.
int run_coordinator(const AppConfig& config, int world_size) {
    if (config.threads > 0) {
        #ifdef _OPENMP
        omp_set_num_threads(config.threads);
        #endif
    }

    cv::VideoCapture capture;

    if (config.use_webcam) {
        capture.open(config.webcam_index);
        std::cout << "Entrada: webcam " << config.webcam_index << std::endl;
    } else {
        capture.open(config.input_path);
        std::cout << "Entrada: arquivo " << config.input_path << std::endl;
    }

    if (!capture.isOpened()) {
        throw std::runtime_error("Não foi possível abrir a entrada de vídeo.");
    }

    const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = capture.get(cv::CAP_PROP_FPS);

    if (fps <= 0.0 || fps > 240.0) {
        fps = 30.0;
    }

    cv::VideoWriter writer;
    const int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');

    writer.open(config.output_path, fourcc, fps, cv::Size(width, height), true);

    if (!writer.isOpened()) {
        throw std::runtime_error("Não foi possível abrir o arquivo de saída: " + config.output_path);
    }

    std::cout << "Vídeo: " << width << "x" << height
              << " | fps=" << fps
              << " | saída=" << config.output_path << std::endl;

    std::cout << "Workers: " << (world_size - 1) << std::endl;
    std::cout << "Threads OpenMP por worker: " << config.threads << std::endl;
    std::cout << "----------------------------------------------" << std::endl;

    if (world_size <= 1) {
        return run_local_mode(config, capture, writer);
    }

    std::cout << "Distribuindo frames entre os workers..." << std::endl;
    std::cout << "Processamento em andamento..." << std::endl;

    std::queue<int> idle_workers;

    for (int worker = 1; worker < world_size; ++worker) {
        idle_workers.push(worker);
    }

    bool reading = true;
    int next_frame_id = 0;
    int pending = 0;
    int next_frame_to_write = 0;

    std::map<int, cv::Mat> reorder_buffer;

    const auto start_total = Clock::now();

    while (reading || pending > 0) {
        // Envia frames enquanto houver worker livre.
        while (reading && !idle_workers.empty()) {
            if (should_stop_by_limit(config, next_frame_id)) {
                reading = false;
                break;
            }

            cv::Mat frame;

            if (!capture.read(frame)) {
                reading = false;
                break;
            }

            const int worker = idle_workers.front();
            idle_workers.pop();

            send_frame_to_worker(worker, next_frame_id, frame, config.filter);

            ++pending;
            ++next_frame_id;
        }

        // Recebe frames processados dos workers.
        if (pending > 0) {
            int source_worker = -1;
            int processed_frame_id = -1;

            cv::Mat processed = receive_processed_frame(source_worker, processed_frame_id);

            --pending;
            idle_workers.push(source_worker);

            // Guarda pelo id original para garantir a ordem correta.
            reorder_buffer[processed_frame_id] = processed;

            // Grava todos os frames que já podem ser escritos em ordem.
            write_ready_frames(reorder_buffer, next_frame_to_write, writer, config.display);
        }
    }

    stop_workers(world_size);

    const auto end_total = Clock::now();

    std::cout << "----------------------------------------------\n"
              << "Execução finalizada com sucesso.\n"
              << "Frames enviados: " << next_frame_id << "\n"
              << "Frames gravados: " << next_frame_to_write << "\n"
              << "Tempo total: " << elapsed_ms(start_total, end_total) << " ms\n"
              << "Arquivo gerado: " << config.output_path << std::endl;

    return 0;
}