#include "pipeline.hpp"       // Contém tags MPI, enum dos filtros e assinatura das funções.
#include <mpi.h>               // Funções MPI usadas para receber e enviar mensagens.
#include <opencv2/opencv.hpp>  // cv::Mat usado para reconstruir o frame recebido em bytes.
#include <chrono>              // Usado para medir o tempo de processamento do frame.
#include <iostream>            // Usado para logs no terminal.
#include <vector>              // Usado como buffer de bytes do frame.

#ifdef _OPENMP
#include <omp.h>               // Usado para configurar e consultar threads OpenMP.
#endif

namespace {

using Clock = std::chrono::high_resolution_clock;

// Quantidade padrão de threads OpenMP usada por worker.
// O comando de apresentação está usando --threads 4, então deixamos o worker coerente com isso.
constexpr int DEFAULT_WORKER_THREADS = 4;

// Calcula quantos bytes um frame ocupa em memória.
size_t frame_bytes(int rows, int cols, int type) {
    return static_cast<size_t>(rows) * static_cast<size_t>(cols) * CV_ELEM_SIZE(type);
}

// Converte a diferença entre dois tempos para milissegundos.
double elapsed_ms(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // fim do namespace anônimo

// Função executada por todos os processos MPI que não são o rank 0.
// Cada worker fica aguardando frames enviados pelo coordenador.
int run_worker(int rank) {
    #ifdef _OPENMP
    // Define explicitamente a quantidade de threads usadas dentro do worker.
    omp_set_num_threads(DEFAULT_WORKER_THREADS);
    #endif

    std::cout << "[Worker " << rank << "] iniciado." << std::endl;

    int processed_frames = 0;
    double total_processing_time = 0.0;

    while (true) {
        MPI_Status status{};
        int meta[5] = {0, 0, 0, 0, 0};

        // Recebe os metadados do frame ou um sinal de parada.
        MPI_Recv(meta, 5, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_STOP) {
            break;
        }

        const int frame_id = meta[0];
        const int rows = meta[1];
        const int cols = meta[2];
        const int type = meta[3];
        const auto filter = static_cast<FilterType>(meta[4]);

        const int bytes = static_cast<int>(frame_bytes(rows, cols, type));

        std::vector<unsigned char> buffer(bytes);

        // Recebe os pixels do frame enviado pelo coordenador.
        MPI_Recv(
            buffer.data(),
            bytes,
            MPI_UNSIGNED_CHAR,
            0,
            TAG_FRAME_DATA,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        // Reconstrói o frame OpenCV a partir dos bytes recebidos.
        cv::Mat input(rows, cols, type, buffer.data());

        const auto start = Clock::now();

        // Aplica o filtro manual.
        // Dentro do filtro, o OpenMP divide o processamento entre threads.
        cv::Mat output = apply_filter(input, filter);

        const auto end = Clock::now();

        const double processing_time = elapsed_ms(start, end);

        ++processed_frames;
        total_processing_time += processing_time;

        // Garante que a imagem de saída esteja contínua em memória antes do envio.
        cv::Mat contiguous = output.isContinuous() ? output : output.clone();

        const int result_meta[4] = {
            frame_id,
            contiguous.rows,
            contiguous.cols,
            contiguous.type()
        };

        const int result_bytes = static_cast<int>(contiguous.total() * contiguous.elemSize());

        // Envia metadados do frame processado.
        MPI_Send(
            result_meta,
            4,
            MPI_INT,
            0,
            TAG_RESULT_META,
            MPI_COMM_WORLD
        );

        // Envia pixels do frame processado.
        MPI_Send(
            contiguous.data,
            result_bytes,
            MPI_UNSIGNED_CHAR,
            0,
            TAG_RESULT_DATA,
            MPI_COMM_WORLD
        );
    }

    const double average_time = processed_frames > 0
        ? total_processing_time / processed_frames
        : 0.0;

    std::cout << "[Worker " << rank << "] encerrado. "
              << "Frames processados: " << processed_frames
              << " | tempo médio: " << average_time << " ms"
              << " | threads=" << DEFAULT_WORKER_THREADS
              << std::endl;

    return 0;
}