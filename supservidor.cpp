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
  // fila para select
  mysocket_queue f;
  // socket temporario
  tcp_mysocket t;
  // comando recebido/ enviado
  uint16_t cmd;
  // dados da nova conexao
  string login, password;

  // Variaveis auxiliares:
  // O status de retorno das funcoes do socket
  mysocket_status iResult;
  // estado dos tanques
  SupState S;
  // iterator para lista de usuarios
  std::list<User>::iterator iU;

  while (server_on) {
    try { // Erros graves: catch encerra o servidor
      // Se socket de conexoes nao estah aceitando conexoes, encerra o servidor
      if (!sock_server.accepting()) throw "socket de conexoes fechado\n"; // Erro grave: encerra o servidor

      // Inclui na fila de sockets para o select todos os sockets que eu
      // quero monitorar para ver se houve chegada de dados
      f.clear();
      // Inclui o socket de conexoes
      f.include(sock_server);
      // Inclui o socket de todos os clientes conectados
      for (auto& U : LU) if (U.isConnected()) f.include(U.sock);
      
      // Espera que chegue algum dado em qualquer dos sockets da fila
      iResult = f.wait_read(SUP_TIMEOUT*1000);

      switch (iResult) { //resultado do wait_read
        case mysocket_status::SOCK_ERROR:
        default:
          // erro no select
          throw 2;
          break;
        case mysocket_status::SOCK_TIMEOUT:
          // Saiu por timeout: nao houve atividade em nenhum socket da fila
          // nao faz nada
          break;
        case mysocket_status::SOCK_OK:
          // Houve atividade em algum socket da fila
          // Testa em qual socket houve atividade.
          try { // Erros nos clientes: catch fecha a conexao com esse cliente
            // Primeiro, testa os sockets dos clientes
            for (iU = LU.begin(); server_on && iU != LU.end(); ++iU) {
              if (server_on && iU->isConnected() && f.had_activity(iU->sock)) {
                // Leh o comando recebido do cliente
                iResult = iU->sock.read_uint16(cmd);

                if (iResult != mysocket_status::SOCK_OK) throw 1;

                // executa o comando lido
                switch (cmd) {
                  case CMD_ADMIN_OK:
                  case CMD_LOGIN:
                  default:
                    throw 2; // comando invalido
                    break;

                  case CMD_GET_DATA:
                  // envia as informações da planta para o cliente
                  iU->sock.write_uint16(CMD_DATA);
                  readStateFromSensors(S);
                  iU->sock.write_uint16(S.V1);
                  iU->sock.write_uint16(S.V2);
                  iU->sock.write_uint16(S.H1);
                  iU->sock.write_uint16(S.H2);
                  iU->sock.write_uint16(S.PumpInput);
                  iU->sock.write_uint16(S.PumpFlow);
                  iU->sock.write_uint16(S.ovfl);
                  break;

                  case CMD_SET_PUMP:
                  if (!iU->isAdmin) {iU->sock.write_uint16(CMD_ERROR); break;}
                  iResult = iU->sock.read_uint16(cmd);
                  if (iResult != mysocket_status::SOCK_OK) throw 3;
                  setPumpInput(cmd);
                  iU->sock.write_uint16(CMD_OK);
                  break;

                  case CMD_SET_V1:
                  if (!iU->isAdmin) {iU->sock.write_uint16(CMD_ERROR); break;}
                  iResult = iU->sock.read_uint16(cmd);
                  if (iResult != mysocket_status::SOCK_OK) throw 3;
                  setV1Open(cmd != 0);
                  iU->sock.write_uint16(CMD_OK);
                  break;

                  case CMD_SET_V2:
                  if (!iU->isAdmin) {iU->sock.write_uint16(CMD_ERROR); break;}
                  iResult = iU->sock.read_uint16(cmd);
                  if (iResult != mysocket_status::SOCK_OK) throw 3;
                  setV2Open(cmd != 0);
                  iU->sock.write_uint16(CMD_OK);
                  break;

                  case CMD_LOGOUT:
                  // desloga kk
                  iU->close();
                  break;

                } // Fim do switch(cmd)
              } // Fim do if (... && had_activity) no socket do cliente
            } // Fim do for para todos os clientes
          } // Fim do try para erros nos clientes
        catch (int e) // erros na leitura do socket de algum cliente
        {
          cerr << "Erro " << e << " na leitura de socket do cliente \n";
        }

        // Depois de testar os sockets dos clientes,
        // testa se houve atividade no socket de conexao
        if (server_on && sock_server.connected() && f.had_activity(sock_server)) {
          // Aceita provisoriamente a nova conexao
          iResult = sock_server.accept(t);
          if (iResult != mysocket_status::SOCK_OK) throw 3; // Erro grave: encerra o servidor

          try { // Erros na conexao de cliente: fecha socket temporario ou desconecta novo cliente
            // Leh o comando
            iResult = t.read_uint16(cmd, SUP_TIMEOUT*1000);
            if (iResult != mysocket_status::SOCK_OK) throw 1;

            // Testa o comando
            if (cmd!=CMD_LOGIN) throw 2;

            // Leh o login do usuario que deseja se conectar
            iResult = t.read_string(login, SUP_TIMEOUT*1000);
            if (iResult != mysocket_status::SOCK_OK) throw 3;

            // Leh a senha do usuario que deseja se conectar
            iResult = t.read_string(password, SUP_TIMEOUT*1000);
            if (iResult != mysocket_status::SOCK_OK) throw 4;

            if (login.size()<6 || login.size()>12 ||
                password.size()<6 || password.size()>12) throw 5;
            // Verifica se jah existe um usuario cadastrado com esse login
            iU = find(LU.begin(), LU.end(), login);
            
            if (iU==LU.end()) throw 6; // nao existe esse usuario na lista
            // Testa se a senha confere
            if (iU->password != password) throw 7; // Senha nao confere
            // Testa se o cliente jah estah conectado
            if (iU->isConnected()) throw 8; // User jah conectado
            // Associa o socket que se conectou a um usuario cadastrado
            iU->sock.swap(t);

            // Envia a confirmacao de conexao para o novo cliente
            if (iU->isAdmin) iResult = iU->sock.write_uint16(CMD_ADMIN_OK);
            else iResult = iU->sock.write_uint16(CMD_OK);
            if (iResult != mysocket_status::SOCK_OK) throw 9;
          

          } // Fim do try para erros na conexao de cliente
          catch (int e) { // Erros na conexao do novo cliente
            if (e >= 5 && e < 9) { 
              // Socket OK mas login invalido
              t.write_uint16(CMD_ERROR);
              t.close();
            }
            else {
              // erro 1-3 ou 9 (comunicacao com socket)
              if (e == 9) {
                iU->close(); // erro na comunicacao com novo cliente
              }
              else {
                t.close(); // socket temporario deu B.O
              }
            }
            // Informa erro nao previsto
              cerr << "Erro " << e << " na conexao de novo cliente" << endl;
          } // fim catch
        } // // fim if (had_activity) no socket de conexoes
        break; // fim do case mysocket_status::SOCK_OK - resultado do wait_read
        
      } // fim do switch (iResult) - resultado do wait_read
      
    } // Fim do try para erros criticos no servidor
    
    catch(const char* e) {
    // erros criticos no servidor
      cerr << "Erro " << e << " no servidor. Encerrando\n";
      server_on = false;
      // Fecha todos os sockets dos clientes
      for (auto& U : LU) U.close();
      // Fecha o socket de conexoes
      sock_server.close();

      // Os tanques continuam funcionando
    }  // fim catch(const char*)
  }  // fim while (server_on)

}





