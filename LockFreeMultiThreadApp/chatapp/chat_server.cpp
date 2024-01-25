#include "chat_server.h"
#include "io_manager.h"

#include <functional>
#include <iostream>
#include <sstream>

bool ChatServer::Initialize() {
  std::cout << __func__ << " : ChatServer Initializing...\n";
  ClientConnectCB client_connect_cb =
      std::bind(&ChatServer::OnClientConnected, this, std::placeholders::_1);
  ClientConnectCB client_disconnect_cb =
    std::bind(&ChatServer::OnClientDisconnected, this, std::placeholders::_1);
  ClientMessageCB client_msg_recvd_cb =
      std::bind(&ChatServer::OnClientMessageRecvd, this,
                                std::placeholders::_1, std::placeholders::_2);

  io_manager_.StartServer(std::move(client_connect_cb),
            std::move(client_disconnect_cb), std::move(client_msg_recvd_cb));

  return true;
}

void ChatServer::OnClientConnected(int id) {
  MAKE_SURE_MAIN_THREAD(&ChatServer::OnClientConnected, id)

  std::cout << __func__ << ": Buddy :" << id << " connected.\n";
  clients_id_.push_back(id);
}

void ChatServer::OnClientDisconnected(int id) {
  MAKE_SURE_MAIN_THREAD(&ChatServer::OnClientDisconnected, id)

  std::cout << __func__ << ": Buddy :" << id << " disconnected.\n";

  clients_id_.erase(std::remove(clients_id_.begin(),
          clients_id_.end(), id), clients_id_.end());
}

void ChatServer::OnClientMessageRecvd(int id, const std::string msg) {
  MAKE_SURE_MAIN_THREAD(&ChatServer::OnClientMessageRecvd, id, msg)

  std::stringstream in_msg;
  in_msg << "Buddy " << id << " : " << msg;

  std::cout << __func__ << ": " << in_msg.str() << std::endl;

  for (auto client : clients_id_) {
    if (client != id) {
      io_manager_.SendMessageToClient(client, in_msg.str());
    }
  }
}
