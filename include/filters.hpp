#ifndef FILTERS_HPP
#define FILTERS_HPP

#include <opencv2/opencv.hpp> // Usado para cv::Mat, cv::Vec3b e tipos de imagem.
#include <string>             // Usado para converter nome textual do filtro.

// Importante para a especificação do trabalho:
// o OpenCV é usado para representar o frame em memória, ler vídeo, exibir e gravar.
// Os filtros abaixo são implementados manualmente, sem cv::cvtColor, cv::filter2D,
// cv::GaussianBlur, cv::blur ou funções equivalentes prontas de processamento.

enum class FilterType {
    GRAYSCALE = 1, // Escala de cinza.
    SHARPEN = 2,   // Nitidez.
    DENOISE = 3    // Redução de ruído por média.
};

// Converte texto digitado no terminal para o enum usado no código.
FilterType filter_from_string(const std::string& name);

// Converte o enum para texto, usado principalmente nos logs.
std::string filter_to_string(FilterType filter);

// Função genérica que chama o filtro correto de acordo com o enum recebido.
cv::Mat apply_filter(const cv::Mat& input, FilterType filter);

// Filtro de escala de cinza manual.
cv::Mat apply_grayscale(const cv::Mat& input);

// Filtro de nitidez manual com kernel 3x3.
cv::Mat apply_sharpen(const cv::Mat& input);

// Filtro de redução de ruído manual com média 3x3.
cv::Mat apply_denoise(const cv::Mat& input);

#endif
