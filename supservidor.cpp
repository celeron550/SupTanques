#include <iostream>     /* cerr */
#include <algorithm>
#include "supservidor.h"

using namespace std;

/* ========================================
   CLASSE SUPSERVIDOR
   ======================================== */

/// Construtor
SupServidor::SupServidor()
  : Tanks()
  , server_on(false)
  , LU()
  , thr_server() 
  , sock_server()
  
{
  // Inicializa a biblioteca de sockets
  mysocket_status iResult = mysocket::init();
  // Em caso de erro, mensagem e encerra
  if (iResult != mysocket_status::SOCK_OK)
  {
    cerr <<  "Biblioteca mysocket nao pode ser inicializada";
    exit(-1);
  }
}

/// Destrutor
SupServidor::~SupServidor()
{
  // Deve parar a thread do servidor
  server_on = false;

  // Fecha todos os sockets dos clientes
  for (auto& U : LU) U.close();
  // Fecha o socket de conexoes
  sock_server.close();

  // Espera o fim da thread do servidor
  if (thr_server.joinable()) thr_server.join();

  // Encerra a biblioteca de sockets
  mysocket::end();
}

/// Liga o servidor
bool SupServidor::setServerOn()
{
  // Se jah estah ligado, nao faz nada
  if (server_on) return true;

  // Liga os tanques
  setTanksOn();

  // Indica que o servidor estah ligado a partir de agora
  server_on = true;

  try
  {
    // Coloca o socket de conexoes em escuta
   mysocket_status iResult = sock_server.listen(SUP_PORT);
    // Em caso de erro, gera excecao
    if (iResult != mysocket_status::SOCK_OK) throw 1;

    // Lanca a thread do servidor que comunica com os clientes
    thr_server = thread( [this]()
    {
      this->thr_server_main();
    } );
    // Em caso de erro, gera excecao
    if (!thr_server.joinable()) throw 2;
  }
  catch(int i)
  {
    cerr << "Erro " << i << " ao iniciar o servidor\n";

    // Deve parar a thread do servidor
    server_on = false;

    // Fecha o socket do servidor
    sock_server.close();

    return false;
  }

  // Tudo OK
  return true;
}

/// Desliga o servidor
void SupServidor::setServerOff()
{
  // Se jah estah desligado, nao faz nada
  if (!server_on) return;

  // Deve parar a thread do servidor
  server_on = false;

  // Fecha todos os sockets dos clientes
  for (auto& U : LU) U.close();
  // Fecha o socket de conexoes
  sock_server.close();

  // Espera pelo fim da thread do servidor
  if (thr_server.joinable()) thr_server.join();
  // Faz o identificador da thread apontar para thread vazia
  thr_server = thread();
  

  // Desliga os tanques
  setTanksOff();
}

/// Leitura do estado dos tanques
void SupServidor::readStateFromSensors(SupState& S) const
{
  // Estados das valvulas: OPEN, CLOSED
  S.V1 = v1isOpen();
  S.V2 = v2isOpen();
  // Niveis dos tanques: 0 a 65535
  S.H1 = hTank1();
  S.H2 = hTank2();
  // Entrada da bomba: 0 a 65535
  S.PumpInput = pumpInput();
  // Vazao da bomba: 0 a 65535
  S.PumpFlow = pumpFlow();
  // Estah transbordando (true) ou nao (false)
  S.ovfl = isOverflowing();
}

/// Leitura e impressao em console do estado da planta
void SupServidor::readPrintState() const
{
  if (tanksOn())
  {
    SupState S;
    readStateFromSensors(S);
    S.print();
  }
  else
  {
    cout << "Tanques estao desligados!\n";
  }
}

/// Impressao em console dos usuarios do servidor
void SupServidor::printUsers() const
{
  for (const auto& U : LU)
  {
    cout << U.login << '\t'
         << "Admin=" << (U.isAdmin ? "SIM" : "NAO") << '\t'
         << "Conect=" << (U.isConnected() ? "SIM" : "NAO") << '\n';
  }
}

/// Adicionar um novo usuario
bool SupServidor::addUser(const string& Login, const string& Senha,
                             bool Admin)
{
  // Testa os dados do novo usuario
  if (Login.size()<6 || Login.size()>12) return false;
  if (Senha.size()<6 || Senha.size()>12) return false;

  // Testa se jah existe usuario com mesmo login
  auto itr = find(LU.begin(), LU.end(), Login);
  if (itr != LU.end()) return false;

  // Insere
  LU.push_back( User(Login,Senha,Admin) );

  // Insercao OK
  return true;
}

/// Remover um usuario
bool SupServidor::removeUser(const string& Login)
{
  // Testa se existe usuario com esse login
  auto itr = find(LU.begin(), LU.end(), Login);
  if (itr == LU.end()) return false;

  // Remove
  LU.erase(itr);

  // Remocao OK
  return true;
}

/// A thread que implementa o servidor.
/// Comunicacao com os clientes atraves dos sockets.
void SupServidor::thr_server_main(void)
{
  // Fila de sockets para aguardar chegada de dados
  mysocket_queue f;
  // Socket temporario
  tcp_mysocket t;
  // Informacao lida do socket
  int16_t cmd;
  // Variaveis auxiliares
  mysocket_status iResult;
  
  string login, senha;
  SupState S;
  std::list<User>::iterator iU;
  
  

  while (server_on)
  {
    // Erros mais graves que encerram o servidor
    // Parametro do throw e do catch eh uma const char* = "texto"
    try
    {
      // Encerra se o socket de conexoes estiver fechado
      if (!sock_server.accepting())
      {
        throw "socket de conexoes fechado\n";
      }

      // Inclui na fila de sockets todos os sockets que eu
      // quero monitorar para ver se houve chegada de dados

      // Limpa a fila de sockets
      f.clear();
      // Inclui na fila o socket de conexoes
      f.include(sock_server);
      // Inclui na fila todos os sockets dos clientes conectados
      for (auto& i : LU) {
        if (i.isConnected()) f.include(i.sock);
      }

      // Espera ateh que chegue dado em algum socket (com timeout)
      iResult = f.wait_read(1000*SUP_TIMEOUT); 
      
      // De acordo com o resultado da espera:
      switch(iResult)
      {
        // SOCK_TIMEOUT:
        case mysocket_status::SOCK_TIMEOUT:
        // Saiu por timeout: nao houve atividade em nenhum socket
        // Aproveita para salvar dados ou entao nao faz nada
          
          break;

        // SOCK_ERROR:
        case mysocket_status::SOCK_ERROR:
        // Erro no select: encerra o servidor
        default:
          throw "Erro no select.\n";
          break;

        // SOCK_OK:
        case mysocket_status::SOCK_OK:
        // Houve atividade em algum socket da fila:
        try {
          //   Testa se houve atividade nos sockets dos clientes. Se sim:
          for(iU = LU.begin(); iU != LU.end(); ++iU)
          {
            if(server_on && iU->isConnected() && f.had_activity(iU->sock))
            {
              //   - Leh o comando
              iResult = iU->sock.read_int16(cmd);
              if (iResult != mysocket_status::SOCK_OK) throw 1;
              //   - Executa a acao
              switch(cmd)
              {
                case CMD_LOGIN:
                case CMD_ADMIN_OK:
                case CMD_OK:
                case CMD_ERROR:
                case CMD_DATA:
                default:
                  throw 2;
                  break;

                case CMD_GET_DATA:
                {
                  iResult = iU->sock.read_int16(cmd, SUP_TIMEOUT*1000);
                  if (iResult !=mysocket_status::SOCK_OK) throw 3;
                  readStateFromSensors(S);
                  iU->sock.write_int16(CMD_DATA);
                  break;
                }
                case CMD_SET_PUMP:
                {
                  uint16_t pumpValue;
                  iResult = iU->sock.read_uint16(pumpValue, SUP_TIMEOUT*1000);
                  if (iResult != mysocket_status::SOCK_OK) throw 4;
                  setPumpInput(pumpValue);
                  break;
                }

                case CMD_SET_V1:
                {
                  uint16_t v1;
                  iResult = iU->sock.read_uint16(v1, SUP_TIMEOUT*1000);
                  if (iResult != mysocket_status::SOCK_OK) throw 5;
                  setV1Open(v1 !=0 ); // revisar 
                  break;
                }

                case CMD_SET_V2:
                {
                  uint16_t v2;
                  iResult = iU->sock.read_uint16(v2, SUP_TIMEOUT*1000);
                  if (iResult != mysocket_status::SOCK_OK) throw 6;
                  setV2Open(v2 !=0 ); // revisar 
                  break;
                case CMD_LOGOUT:
                // desloga kk
                  iU->close();

                if (server_on && f.had_activity(iU->sock))
                {
                  iResult = sock_server.accept(t);
                  if (iResult != mysocket_status::SOCK_OK) throw 7;
                  
                  //   - Leh comando, login e senha
                  iResult = t.read_int16(cmd, SUP_TIMEOUT*1000);
                  if (iResult != mysocket_status::SOCK_OK) throw 8;

                  iResult = t.read_string(login,  SUP_TIMEOUT*1000);
                  if (iResult != mysocket_status::SOCK_OK) throw 9;

                  iResult = t.read_string(senha, SUP_TIMEOUT*1000);
                  if (iResult != mysocket_status::SOCK_OK) throw 10;

                  bool achou = false;
                  for (auto&u : LU) {
                    if (u.login == login && u.password == senha) {
                    achou = true;

                    // Fecha conexão anterior se houver
                    if (u.isConnected()) u.close();

                    // Associa novo socket ao usuário
                    u.sock.swap(t);
                    u.sock.write_int16(CMD_OK);
                    

                    // Envia confirmação
                    iResult = u.sock.write_int16(CMD_OK);
                    if (iResult != mysocket_status::SOCK_OK) {
                      u.close();
                      throw 11;
                    }

                    break;
                  }
                  if (!achou) throw 12;
                }
                  }

                } 

              }
            }
          }
      }

      }
      //   = Envia resposta
      //   Depois, testa se houve atividade no socket de conexao. Se sim:
      //   - Estabelece nova conexao em socket temporario
      //   - Testa usuario
      //   - Se deu tudo certo, faz o socket temporario ser o novo socket
      //     do cliente e envia confirmacao

    } // fim try - Erros mais graves que encerram o servidor
    catch(const char* err)  // Erros mais graves que encerram o servidor
    {
      cerr << "Erro no servidor: " << err << endl;

      // Sai do while e encerra a thread
      server_on = false;

      // Fecha todos os sockets dos clientes
      for (auto& U : LU) U.close();
      // Fecha o socket de conexoes
      sock_server.close();

      // Os tanques continuam funcionando

    } // fim catch - Erros mais graves que encerram o servidor
  } // fim while (server_on)




