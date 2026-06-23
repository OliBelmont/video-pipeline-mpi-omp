#include "filters.hpp"  // Declara o enum FilterType e as funções de filtro.
#include <algorithm>    // Usado em std::transform para normalizar texto.
#include <cctype>       // Usado em std::tolower.
#include <stdexcept>    // Usado para lançar erros quando o filtro ou formato é inválido.
#ifdef _OPENMP
#include <omp.h>        // Usado apenas quando o projeto é compilado com suporte a OpenMP.
#endif

namespace {

// Padroniza o nome do filtro para minúsculo.
// Assim o usuário pode digitar Sharpen, SHARPEN ou sharpen e o código trata igual.
std::string normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return value;
}

// Limita um valor inteiro para o intervalo válido de um canal de cor: 0 a 255.
// Isso é necessário porque filtros como sharpen podem gerar valores negativos ou acima de 255.
inline unsigned char clamp_int(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<unsigned char>(value);
}

} // fim do namespace anônimo

// Converte o filtro informado por texto em um valor do enum FilterType.
FilterType filter_from_string(const std::string& name) {
    const std::string value = normalize(name);

    if (value == "gray" || value == "grayscale" || value == "cinza" || value == "escala_cinza") {
        return FilterType::GRAYSCALE;
    }

    if (value == "sharpen" || value == "nitidez") {
        return FilterType::SHARPEN;
    }

    if (value == "denoise" || value == "noise" || value == "ruido" || value == "reduzir_ruido") {
        return FilterType::DENOISE;
    }

    throw std::invalid_argument("Filtro inválido: " + name + ". Use grayscale, sharpen ou denoise.");
}

// Converte o enum do filtro para texto, facilitando a impressão dos logs.
std::string filter_to_string(FilterType filter) {
    switch (filter) {
        case FilterType::GRAYSCALE: return "grayscale";
        case FilterType::SHARPEN: return "sharpen";
        case FilterType::DENOISE: return "denoise";
        default: return "unknown";
    }
}

// Função central de filtros.
// Ela recebe um frame e decide qual função específica deve ser executada.
cv::Mat apply_filter(const cv::Mat& input, FilterType filter) {
    switch (filter) {
        case FilterType::GRAYSCALE: return apply_grayscale(input);
        case FilterType::SHARPEN: return apply_sharpen(input);
        case FilterType::DENOISE: return apply_denoise(input);
        default: throw std::invalid_argument("Filtro desconhecido");
    }
}

// Aplica escala de cinza manualmente.
// Entrada esperada: frame colorido BGR de 8 bits e 3 canais, formato comum do OpenCV para vídeo.
cv::Mat apply_grayscale(const cv::Mat& input) {
    if (input.empty()) return input.clone(); // Evita processar frame vazio.

    if (input.type() != CV_8UC3) {
        throw std::runtime_error("apply_grayscale espera frame CV_8UC3/BGR.");
    }

    cv::Mat output(input.rows, input.cols, input.type()); // Cria uma imagem de saída com mesmo tamanho e tipo da entrada.

    // OpenMP: divide o laço das linhas entre várias threads.
    // Como cada linha é independente, não há disputa de dados entre as threads.
    #pragma omp parallel for schedule(static)
    for (int row = 0; row < input.rows; ++row) {
        const cv::Vec3b* in_ptr = input.ptr<cv::Vec3b>(row); // Ponteiro para a linha atual da imagem original.
        cv::Vec3b* out_ptr = output.ptr<cv::Vec3b>(row);     // Ponteiro para a linha atual da imagem de saída.

        for (int col = 0; col < input.cols; ++col) {
            const unsigned char b = in_ptr[col][0]; // Canal azul no padrão BGR.
            const unsigned char g = in_ptr[col][1]; // Canal verde.
            const unsigned char r = in_ptr[col][2]; // Canal vermelho.

            // Fórmula clássica de luminância ponderada.
            // O olho humano percebe mais o verde, depois o vermelho e menos o azul.
            const unsigned char gray = clamp_int(static_cast<int>(0.114 * b + 0.587 * g + 0.299 * r));

            out_ptr[col] = cv::Vec3b(gray, gray, gray); // Mantém 3 canais, mas todos com o mesmo valor de cinza.
        }
    }

    return output;
}

// Aplica o filtro sharpen manualmente.
// O objetivo é destacar detalhes e bordas aumentando a diferença entre o pixel central e seus vizinhos.
cv::Mat apply_sharpen(const cv::Mat& input) {
    if (input.empty()) return input.clone();

    if (input.type() != CV_8UC3) {
        throw std::runtime_error("apply_sharpen espera frame CV_8UC3/BGR.");
    }

    // Começa copiando a imagem original.
    // As bordas ficam preservadas porque o kernel 3x3 não consegue ser aplicado nelas sem sair da matriz.
    cv::Mat output = input.clone();

    // Kernel manual 3x3 aplicado em cada canal B, G e R:
    //  0 -1  0
    // -1  5 -1
    //  0 -1  0
    //
    // Fórmula prática:
    // novo_pixel = 5 * centro - esquerda - direita - cima - baixo
    #pragma omp parallel for schedule(static)
    for (int row = 1; row < input.rows - 1; ++row) { // Começa em 1 e termina antes da última linha para evitar acesso fora da imagem.
        const cv::Vec3b* prev = input.ptr<cv::Vec3b>(row - 1); // Linha acima.
        const cv::Vec3b* curr = input.ptr<cv::Vec3b>(row);     // Linha atual.
        const cv::Vec3b* next = input.ptr<cv::Vec3b>(row + 1); // Linha abaixo.
        cv::Vec3b* out_ptr = output.ptr<cv::Vec3b>(row);       // Linha de saída.

        for (int col = 1; col < input.cols - 1; ++col) { // Ignora primeira e última coluna pelo mesmo motivo das bordas.
            cv::Vec3b result; // Pixel calculado para a saída.

            for (int channel = 0; channel < 3; ++channel) { // Processa B, G e R separadamente.
                const int value = 5 * curr[col][channel]
                                - curr[col - 1][channel]
                                - curr[col + 1][channel]
                                - prev[col][channel]
                                - next[col][channel];

                result[channel] = clamp_int(value); // Garante que o canal fique entre 0 e 255.
            }

            out_ptr[col] = result; // Grava o pixel processado.
        }
    }

    return output;
}

// Aplica redução de ruído por média 3x3.
// A ideia é substituir cada pixel pela média dele com seus oito vizinhos.
cv::Mat apply_denoise(const cv::Mat& input) {
    if (input.empty()) return input.clone();

    if (input.type() != CV_8UC3) {
        throw std::runtime_error("apply_denoise espera frame CV_8UC3/BGR.");
    }

    // Copia a entrada para manter as bordas originais.
    // Apenas os pixels internos são recalculados.
    cv::Mat output = input.clone();

    #pragma omp parallel for schedule(static)
    for (int row = 1; row < input.rows - 1; ++row) { // Percorre somente as linhas internas.
        cv::Vec3b* out_ptr = output.ptr<cv::Vec3b>(row); // Ponteiro da linha de saída.

        for (int col = 1; col < input.cols - 1; ++col) { // Percorre somente as colunas internas.
            int sum_b = 0; // Soma dos valores azuis da vizinhança 3x3.
            int sum_g = 0; // Soma dos valores verdes da vizinhança 3x3.
            int sum_r = 0; // Soma dos valores vermelhos da vizinhança 3x3.

            for (int dy = -1; dy <= 1; ++dy) { // Variação vertical: linha anterior, atual e próxima.
                const cv::Vec3b* in_ptr = input.ptr<cv::Vec3b>(row + dy);

                for (int dx = -1; dx <= 1; ++dx) { // Variação horizontal: coluna anterior, atual e próxima.
                    const cv::Vec3b& pixel = in_ptr[col + dx];
                    sum_b += pixel[0];
                    sum_g += pixel[1];
                    sum_r += pixel[2];
                }
            }

            // Divide por 9 porque uma janela 3x3 possui 9 pixels.
            out_ptr[col] = cv::Vec3b(
                static_cast<unsigned char>(sum_b / 9),
                static_cast<unsigned char>(sum_g / 9),
                static_cast<unsigned char>(sum_r / 9)
            );
        }
    }

    return output;
}
