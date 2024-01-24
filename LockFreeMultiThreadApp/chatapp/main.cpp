#include "chat_server.h"

int main() {
  ChatServer chatServer;
  RunnerManager runner_manager;
  runner_manager.BootStrap(chatServer);
  return 0;
}
