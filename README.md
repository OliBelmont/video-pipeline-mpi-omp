# Sistema Distribuído de Processamento de Vídeo em Tempo Real

Disciplina: **Programação Paralela e Distribuída**  
Grupo: **Luís, Pedro e Luiza**

Este projeto implementa um sistema distribuído para processamento de vídeo usando **MPI**, **OpenMP** e **OpenCV**.

A ideia geral é simples: o processo principal lê os frames de um vídeo ou da webcam, distribui esses frames para outros processos usando MPI, cada trabalhador aplica um filtro na imagem usando OpenMP, e depois o processo principal recebe os frames processados e grava o vídeo final.

O OpenCV é usado apenas para abrir o vídeo, representar os frames em `cv::Mat`, exibir a imagem e gravar o resultado. Os filtros foram implementados manualmente no código, sem usar funções prontas como `cv::cvtColor`, `cv::filter2D`, `cv::GaussianBlur` ou `cv::blur`.

---

## 1. Estrutura atual do projeto

```text
video-pipeline-mpi-omp/
├── include/
│   ├── filters.hpp
│   └── pipeline.hpp
├── src/
│   ├── main.cpp
│   ├── coordinator.cpp
│   ├── worker.cpp
│   └── filters.cpp
├── scripts/
│   ├── run_file.sh
│   └── run_webcam.sh
├── examples/
│   └── coloque_aqui_um_video.mp4
├── Makefile
└── README.md
```
---

## 2. O que cada arquivo faz

### `src/main.cpp`

É o ponto de entrada do programa.

Responsabilidades principais:

- inicializa o MPI com `MPI_Init`;
- descobre o `rank` do processo atual;
- descobre o total de processos com `MPI_Comm_size`;
- lê os argumentos informados no terminal;
- decide se o processo será coordenador ou trabalhador;
- finaliza o MPI com `MPI_Finalize`.

Na prática:

```text
rank 0       -> executa run_coordinator(...)
rank 1,2,3... -> executam run_worker(...)
```

### `src/coordinator.cpp`

É o arquivo do processo coordenador, ou seja, o `rank 0`.

Responsabilidades principais:

- abrir o vídeo ou a webcam;
- criar o arquivo de saída;
- ler frame por frame;
- enviar frames para os workers disponíveis;
- receber frames processados;
- reorganizar os frames na ordem correta;
- gravar e/ou exibir o vídeo final;
- enviar sinal de parada para os workers.

Funções importantes:

```text
send_frame_to_worker(...)
```

Envia para um worker:

- id do frame;
- quantidade de linhas;
- quantidade de colunas;
- tipo do frame no OpenCV;
- filtro escolhido;
- bytes reais da imagem.

```text
receive_processed_frame(...)
```

Recebe o frame processado de qualquer worker que terminar primeiro.

```text
write_ready_frames(...)
```

Garante que os frames sejam gravados na ordem correta. Isso é importante porque o worker 2 pode terminar o frame 5 antes do worker 1 terminar o frame 4, por exemplo.

```text
stop_workers(...)
```

Envia `TAG_STOP` para todos os workers quando não existem mais frames para processar.

---

### `src/worker.cpp`

É o arquivo dos processos trabalhadores, ou seja, os ranks maiores que 0.

Responsabilidades principais:

- ficar aguardando frames enviados pelo coordenador;
- receber os metadados do frame;
- receber os bytes da imagem;
- reconstruir o frame como `cv::Mat`;
- aplicar o filtro solicitado;
- devolver o frame processado ao coordenador;
- encerrar quando receber o sinal `TAG_STOP`.

Fluxo básico do worker:

```text
1. Recebe metadados do frame
2. Recebe bytes do frame
3. Monta cv::Mat
4. Aplica filtro manual com OpenMP
5. Envia resultado para o rank 0
6. Volta a esperar novo frame
```

Esse arquivo é bom para explicar a parte de recepção, processamento e retorno dos frames.

---

### `src/filters.cpp`

Contém os filtros de imagem implementados manualmente.

Filtros disponíveis:

```text
grayscale -> escala de cinza
sharpen   -> nitidez
denoise   -> redução de ruído por média 3x3
```

O ponto mais importante deste arquivo é o uso de OpenMP:

```cpp
#pragma omp parallel for schedule(static)
```

Essa diretiva divide o laço das linhas da imagem entre várias threads. Como cada linha pode ser processada de forma independente, é uma divisão simples e segura para demonstrar paralelismo com memória compartilhada.

Exemplo conceitual:

```text
Frame 1280x720 com 4 threads:
Thread 1 -> processa parte das linhas
Thread 2 -> processa parte das linhas
Thread 3 -> processa parte das linhas
Thread 4 -> processa parte das linhas
```

---

### `include/pipeline.hpp`

É o header com as definições principais do pipeline.

Ele contém:

- tags MPI;
- estrutura `AppConfig`;
- assinatura de `run_coordinator`;
- assinatura de `run_worker`.

As tags MPI servem para identificar o tipo de mensagem:

```text
TAG_FRAME_META   -> metadados do frame original
TAG_FRAME_DATA   -> bytes do frame original
TAG_RESULT_META  -> metadados do frame processado
TAG_RESULT_DATA  -> bytes do frame processado
TAG_STOP         -> mensagem de parada dos workers
```

---

### `include/filters.hpp`

É o header dos filtros.

Ele contém:

- enum `FilterType`;
- assinatura das funções de filtro;
- funções de conversão entre texto e enum.

Exemplo:

```text
--filter sharpen
```

O texto `sharpen` é convertido para:

```cpp
FilterType::SHARPEN
```

---

### `scripts/run_file.sh`

Script para facilitar a execução usando arquivo de vídeo.

Ele faz basicamente isso:

```text
1. recebe o caminho do vídeo
2. recebe o filtro
3. recebe a quantidade de processos MPI
4. recebe a quantidade de threads OpenMP
5. executa make
6. roda o mpirun com os parâmetros informados
```

Exemplo:

```bash
./scripts/run_file.sh examples/video_teste.mp4 sharpen 4 4
```

Isso executa:

```text
4 processos MPI
filtro sharpen
4 threads OpenMP por worker
entrada examples/video_teste.mp4
saída output_sharpen.avi
```

---

### `scripts/run_webcam.sh`

Script para facilitar a execução usando webcam.

Exemplo:

```bash
./scripts/run_webcam.sh denoise 4 4
```

Isso executa:

```text
4 processos MPI
filtro denoise
4 threads OpenMP por worker
entrada webcam 0
saída output_webcam_denoise.avi
```

---

### `Makefile`

Arquivo usado para compilar e executar o projeto sem precisar digitar o comando completo do compilador.

Principais comandos:

```bash
make
```

Compila o projeto e gera:

```text
bin/video_pipeline
```

```bash
make clean
```

Remove arquivos gerados, como a pasta `bin` e vídeos `.avi` de saída.

```bash
make run-file
```

Compila e executa um teste usando:

```text
examples/video_teste.mp4
```

Antes de usar esse comando, é necessário colocar um vídeo real nesse caminho.

```bash
make run-webcam
```

Compila e executa usando a webcam 0.

---

## 3. Dependências

Em Ubuntu/Debian, instale com:

```bash
sudo apt update
sudo apt install -y build-essential openmpi-bin libopenmpi-dev libopencv-dev pkg-config
```

Verifique se o OpenCV foi encontrado:

```bash
pkg-config --modversion opencv4
```

Se aparecer uma versão, por exemplo `4.5.4`, está correto.

---

## 4. Compilação

Na raiz do projeto, execute:

```bash
make clean
make
```

O executável será criado em:

```text
bin/video_pipeline
```

O comando `make` faz a compilação usando:

```text
mpicxx
```

Isso é importante porque o projeto usa MPI. Além disso, o Makefile adiciona a flag:

```text
-fopenmp
```

Essa flag ativa o OpenMP no código.

---

## 5. Execução com vídeo

Coloque um vídeo curto dentro da pasta `examples` com o nome:

```text
video_teste.mp4
```

Depois execute:

```bash
mpirun -np 4 ./bin/video_pipeline --input examples/video_teste.mp4 --filter sharpen --output output_sharpen.avi --threads 4 --max-frames 120
```

Explicação do comando:

```text
mpirun
```

Executa um programa usando MPI.

```text
-np 4
```

Cria 4 processos MPI:

```text
rank 0 -> coordenador
rank 1 -> worker
rank 2 -> worker
rank 3 -> worker
```

```text
./bin/video_pipeline
```

Executável gerado pelo Makefile.

```text
--input examples/video_teste.mp4
```

Arquivo de vídeo usado como entrada.

```text
--filter sharpen
```

Filtro que será aplicado nos frames.

```text
--output output_sharpen.avi
```

Nome do vídeo final processado.

```text
--threads 4
```

Quantidade de threads OpenMP usadas por worker.

```text
--max-frames 120
```

Processa somente os primeiros 120 frames. Isso ajuda nos testes porque evita processar um vídeo inteiro muito grande.

---

## 6. Execução com webcam

```bash
mpirun -np 4 ./bin/video_pipeline --webcam 0 --filter denoise --output output_webcam.avi --threads 4 --max-frames 120
```

Esse comando usa a webcam principal do computador.

Para ambiente sem interface gráfica, use:

```bash
mpirun -np 4 ./bin/video_pipeline --input examples/video_teste.mp4 --filter grayscale --output output_gray.avi --threads 4 --max-frames 120 --display 0
```

O parâmetro `--display 0` evita abrir janela do OpenCV.

---

## 7. Execução pelos scripts

Com arquivo:

```bash
./scripts/run_file.sh examples/video_teste.mp4 sharpen 4 4
```

Com webcam:

```bash
./scripts/run_webcam.sh denoise 4 4
```

Esses scripts apenas montam o comando `mpirun` de forma mais prática. Eles não fazem nada diferente do comando manual.

---

## 8. Filtros implementados

### Escala de cinza

Comando:

```text
grayscale
```

Para cada pixel, o código lê os canais B, G e R e calcula um valor de cinza:

```text
cinza = 0.114 * B + 0.587 * G + 0.299 * R
```

Depois grava esse mesmo valor nos três canais:

```text
B = cinza
G = cinza
R = cinza
```

---

### Sharpen

Comando:

```text
sharpen
```

Usa um kernel 3x3 manual:

```text
 0 -1  0
-1  5 -1
 0 -1  0
```

Na prática, ele aumenta o peso do pixel central e subtrai os vizinhos de cima, baixo, esquerda e direita. Isso destaca bordas e detalhes da imagem.

---

### Denoise

Comando:

```text
denoise
```

Usa média 3x3 manual. Cada pixel interno vira a média dele com os oito pixels ao redor.

Isso suaviza pequenas variações e reduz ruídos, mas também pode deixar a imagem um pouco menos nítida.

---

## 9. Como o MPI é usado

O MPI é usado para distribuir os frames entre processos diferentes.

Fluxo:

```text
1. Rank 0 lê um frame
2. Rank 0 envia o frame para um worker livre
3. Worker recebe o frame
4. Worker processa o frame
5. Worker devolve o frame processado
6. Rank 0 grava o frame na ordem correta
```

O coordenador não espera sempre o mesmo worker. Ele recebe o resultado de qualquer worker que terminar primeiro usando:

```cpp
MPI_ANY_SOURCE
```

Isso deixa o pipeline mais flexível, porque um worker pode terminar antes do outro.

---

## 10. Como o OpenMP é usado

O OpenMP é usado dentro dos filtros, no processamento dos pixels.

A divisão foi feita por linhas:

```cpp
#pragma omp parallel for schedule(static)
for (int row = 0; row < input.rows; ++row) {
    // processa pixels da linha
}
```

Cada thread fica responsável por uma parte das linhas do frame.

Essa estratégia é simples de explicar e funciona bem porque uma linha pode ser processada independentemente das outras na escala de cinza. Nos filtros 3x3, cada thread apenas lê linhas vizinhas, mas escreve somente na sua própria posição de saída.

---

## 11. Distribuição sugerida para apresentação

### Luís

Arquivos para explicar:

```text
src/coordinator.cpp
include/pipeline.hpp
scripts/run_file.sh
scripts/run_webcam.sh
```

Pontos principais:

- rank 0 como coordenador;
- leitura dos frames;
- envio de metadados e bytes com MPI;
- fila de workers disponíveis;
- recebimento dos frames processados;
- reordenação dos frames;
- gravação do vídeo final;
- encerramento dos workers com `TAG_STOP`.

---

### Luiza

Arquivos para explicar:

```text
src/filters.cpp
include/filters.hpp
```

Pontos principais:

- filtros implementados manualmente;
- escala de cinza;
- sharpen com kernel 3x3;
- denoise com média 3x3;
- uso de `#pragma omp parallel for schedule(static)`;
- divisão do processamento por linhas;
- motivo de não usar filtros prontos do OpenCV.

---

### Pedro

Arquivos para explicar:

```text
src/main.cpp
src/worker.cpp
Makefile
```

Pontos principais:

- inicialização do MPI;
- leitura dos argumentos do terminal;
- decisão entre coordenador e worker;
- loop do worker aguardando frames;
- reconstrução do `cv::Mat` a partir dos bytes recebidos;
- devolução do frame processado;
- comandos de compilação;
- scripts de execução;
- organização geral do projeto.

---

## 12. Argumentos disponíveis

```text
--input <arquivo.mp4|arquivo.avi>      Usa arquivo de vídeo como entrada
--webcam [indice]                      Usa webcam como entrada
--filter <grayscale|sharpen|denoise>   Filtro aplicado
--output <arquivo.avi>                 Arquivo de saída
--max-frames <n>                       Limita frames processados. 0 = todos
--threads <n>                          Threads OpenMP por worker
--display <0|1>                        Exibe ou não a janela do OpenCV
--help                                 Mostra ajuda no terminal
```

---

## 13. Comandos para testar

Compilar:

```bash
make clean && make
```

Rodar escala de cinza:

```bash
mpirun -np 4 ./bin/video_pipeline --input examples/video_teste.mp4 --filter grayscale --output output_gray.avi --threads 4 --max-frames 120
```

Rodar sharpen:

```bash
mpirun -np 4 ./bin/video_pipeline --input examples/video_teste.mp4 --filter sharpen --output output_sharpen.avi --threads 4 --max-frames 120
```

Rodar denoise:

```bash
mpirun -np 4 ./bin/video_pipeline --input examples/video_teste.mp4 --filter denoise --output output_denoise.avi --threads 4 --max-frames 120
```

Rodar sem abrir janela:

```bash
mpirun -np 4 ./bin/video_pipeline --input examples/video_teste.mp4 --filter sharpen --output output_sharpen.avi --threads 4 --max-frames 120 --display 0
```

Limpar arquivos gerados:

```bash
make clean
```

---

## 15. Observações finais

Rodar com pelo menos dois processos MPI:

```bash
mpirun -np 2 ./bin/video_pipeline ...
```

Com `-np 2`, teremos:

```text
1 coordenador
1 worker
```

Com `-np 4`, teremos:

```text
1 coordenador
3 workers
```
