
#ifndef CHAT_SERVER_H_
#define CHAT_SERVER_H_

#include "io_manager.h"
#include "task_runner.h"

#include <string>
#include <vector>

class IoManager;

class ChatServer : public Application{
public:
  bool Initialize() override;

private:
  void OnClientConnected(int id);

  void OnClientDisconnected(int id);

  void OnClientMessageRecvd(int id, const std::string msg);

private:
  std::vector<int> clients_id_; //Here, I use clients' FD as identifiers  
  IoManager io_manager_;
};

#endif //CHAT_SERVER_H_
