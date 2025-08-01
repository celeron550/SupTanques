# SupTanques

## Descrição

O **SupTanques** é um sistema de supervisão e controle de tanques implementado em C++. O projeto simula um ambiente industrial onde clientes podem monitorar e controlar remotamente o estado de tanques, válvulas e bombas através de uma arquitetura cliente-servidor.

O sistema foi desenvolvido como exercício prático de **programação paralela** (uso de threads) e **comunicação em rede** (sockets TCP), permitindo múltiplos clientes conectados simultaneamente ao servidor.

> ⚠️ **Atenção:** Este projeto fez parte de uma **avaliação da disciplina de Programação Avançada (DCA3303)**. O código foi submetido a um software detector de plágio. **Não é recomendado copiar qualquer trecho deste repositório para fins acadêmicos**, pois isso pode ser facilmente detectado.
---

## Funcionalidades

- **Servidor**:
  - Gerencia múltiplos usuários (admin e comuns).
  - Aceita conexões simultâneas de clientes via sockets TCP.
  - Executa comandos de leitura e controle dos tanques, válvulas e bomba.
  - Utiliza uma thread dedicada para gerenciar as conexões e comandos dos clientes.

- **Cliente**:
  - Possui interface de console e visual (feita em QT) para interação com o usuário.
  - Permite login, consulta do estado dos tanques e envio de comandos de controle.
  - Utiliza uma thread paralela para atualização periódica dos dados do sistema.
  - Sincronização de acesso ao console e ao socket usando mutex.

---

## Tecnologias e Conceitos Trabalhados

- **Programação Paralela**:
  - Uso de threads (`std::thread`) para permitir que o servidor atenda múltiplos clientes simultaneamente.
  - No cliente, uma thread separada faz a atualização periódica dos dados enquanto o usuário interage com o menu.

- **Sockets TCP**:
  - Comunicação entre cliente e servidor usando sockets TCP (implementação multiplataforma).
  - Protocolo simples para troca de comandos e dados entre as aplicações.

- **Sincronização**:
  - Uso de mutex (`std::mutex`) para evitar condições de corrida e garantir a integridade dos dados e da saída no console.

---

## Como compilar

### Windows

1. Certifique-se de que o bloco de código para Windows está descomentado em `mysocket.cpp` e `mysocket.h`.
2. Compile usando um compilador C++11 ou superior (ex: MinGW, MSVC).
3. Link com a biblioteca `Ws2_32` (no MinGW: `-lws2_32`).

### Linux

1. Descomente o bloco de código para Linux em `mysocket.cpp` e `mysocket.h`.
2. Compile usando um compilador C++11 ou superior (ex: g++).
3. Link com a biblioteca `pthread` (`-lpthread`).

### Compilando a Interface Gráfica do Cliente [Opcional]

1. Certifique-se de ter o **Qt 6** instalado (Qt Creator recomendado).
2. Abra o projeto `SupCliente.pro`  da interface gráfica do cliente no Qt Creator.
3. Configure o kit de compilação para Qt 6.
4. Compile e execute o projeto pelo Qt Creator ou utilize os comandos do Qt (`qmake`).
5. **Observação:** A interface gráfica depende das mesmas bibliotecas de socket e lógica do cliente console, portanto, mantenha os arquivos do projeto organizados e acessíveis.

---

## Como usar

1. **Inicie o servidor**:
   - Execute o programa do servidor.
   - Adicione usuários conforme necessário.
   - Ligue o servidor para começar a aceitar conexões.

2. **Inicie o cliente**:
   - Execute o programa do cliente.
   - Informe o IP do servidor, login e senha.
   - Use o menu para consultar e controlar o sistema.

3. **Comandos disponíveis**:
   - Consultar estado dos tanques.
   - Alterar entrada da bomba.
   - Abrir/fechar válvulas.
   - Desconectar e sair.

---

## Estrutura do Projeto

- `supservidor.cpp` / `supservidor.h`: Implementação do servidor.
- `supcliente.cpp` / `supcliente.h`: Implementação do cliente base.
- `supcliente_term.cpp` / `supcliente_term.h`: Interface de console do cliente.
- `mysocket.cpp` / `mysocket.h`: Implementação multiplataforma de sockets TCP.
- `tanques.h`: Simulação dos tanques e sensores.
- `supdados.h`: Definições de comandos e estruturas de dados.

---

## Observações

- O projeto é de cunho didático, podendo portanto apresentar bugs / instabilidades durante o uso.
- O uso correto de threads e mutex é fundamental para evitar problemas de concorrência e garantir a integridade da comunicação e da interface.

---

## Licença

Este projeto é livre para estudos e adaptações. Uso para fins acadêmicos é desencorajado devido à natureza da avaliação mencionada.
