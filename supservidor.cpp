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
  mysocket_queue f;
  tcp_mysocket t;
  uint16_t cmd;
  mysocket_status iResult;
  string login, senha;
  SupState S;
  std::list<User>::iterator iU;

  while (server_on)
  {
    try
    {
      if (!sock_server.accepting())
        throw 1;

      // Monta a fila de sockets
      f.clear();
      f.include(sock_server);
      for (auto& u : LU)
        if (u.isConnected()) f.include(u.sock);

      // Espera atividade
      iResult = f.wait_read(1000 * SUP_TIMEOUT);

      switch (iResult)
      {
      case mysocket_status::SOCK_ERROR:
      default:
        throw 2;
        break;

      case mysocket_status::SOCK_TIMEOUT:
        // Timeout: nada a fazer
        break;

      case mysocket_status::SOCK_OK:
        try
        {
          // Primeiro, testa os sockets dos clientes
          for (iU = LU.begin(); server_on && iU != LU.end(); ++iU)
          {
            if (iU->isConnected() && f.had_activity(iU->sock))
            {
              // Leh o comando recebido do cliente
              iResult = iU->sock.read_uint16(cmd);
              if (iResult != mysocket_status::SOCK_OK) throw 3;

              // Executa o comando lido
              switch (cmd)
              {
              case CMD_GET_DATA:
                readStateFromSensors(S);
                iU->sock.write_uint16(S.H1);
                iU->sock.write_uint16(S.H2);
                iU->sock.write_uint16(S.ovfl);
                iU->sock.write_uint16(S.PumpFlow);
                iU->sock.write_uint16(S.PumpInput);
                iU->sock.write_uint16(S.V1);
                iU->sock.write_uint16(S.V2);
                break;

              case CMD_SET_PUMP:
              {
                uint16_t pumpValue;
                iResult = iU->sock.read_uint16(pumpValue, SUP_TIMEOUT * 1000);
                if (!iU->isAdmin) throw -24;
                if (iResult != mysocket_status::SOCK_OK) throw 5;
                setPumpInput(pumpValue);
                break;
              }
              case CMD_SET_V1:
              {
                uint16_t v1;
                iResult = iU->sock.read_uint16(v1, SUP_TIMEOUT * 1000);
                if (!iU->isAdmin) throw -24;
                if (iResult != mysocket_status::SOCK_OK) throw 6;
                setV1Open(v1 != 0);
                break;
              }
              case CMD_SET_V2:
              {
                uint16_t v2;
                iResult = iU->sock.read_uint16(v2, SUP_TIMEOUT * 1000);
                if (!iU->isAdmin) throw -24;
                if (iResult != mysocket_status::SOCK_OK) throw 7;
                setV2Open(v2 != 0);
                break;
              }
              case CMD_LOGOUT:
                iU->close();
                break;

              default:
                // Comando desconhecido, pode fechar o socket ou ignorar
                break;
              }
            }
          }

          // 2. Aceita novas conexões se houver atividade no socket de conexões
          if (server_on && sock_server.connected() && f.had_activity(sock_server))
          {
            iResult = sock_server.accept(t);
            if (iResult != mysocket_status::SOCK_OK) throw -1;

            // Lê comando, login e senha do novo cliente
            iResult = t.read_uint16(cmd, SUP_TIMEOUT * 1000);
            if (iResult != mysocket_status::SOCK_OK) throw -2;
            if (cmd != CMD_LOGIN) throw -3;

            iResult = t.read_string(login, SUP_TIMEOUT * 1000);
            if (iResult != mysocket_status::SOCK_OK) throw -4;
            iResult = t.read_string(senha, SUP_TIMEOUT * 1000);
            if (iResult != mysocket_status::SOCK_OK) throw -5;

            // Procura usuário
            iU = find(LU.begin(), LU.end(), login);
            if (iU == LU.end() || iU->password != senha || iU->isConnected())
            {
              // Login inválido, senha errada ou já conectado
              t.write_uint16(CMD_ERROR);
              throw -6;
              break;
            }

            // Associa o socket ao usuário
            iU->sock.swap(t);
            iU->sock.write_uint16(CMD_OK);
          }
        }
        catch (...)
        {
          t.close();
        }
        break;
      }
    }
    catch (const char* err)
    {
      cerr << "Erro no servidor: " << err << endl;
      server_on = false;
      for (auto& U : LU) U.close();
      sock_server.close();
    }
  }
}





