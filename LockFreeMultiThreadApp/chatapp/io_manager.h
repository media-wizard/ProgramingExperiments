
#ifndef IO_MANAGER_H_
#define IO_MANAGER_H_

#include "task_runner.h"

#include <functional>
#include <string>
#include <unordered_map>


#define RUN_ON_THREAD(type, method, ...)                              \
  std::packaged_task<void()> type##_task(                             \
                          std::bind(method, this, ##__VA_ARGS__));    \
  TaskRunner::GetTaskRunner(RunnerType::type)->PostTask(type##_task);


#define MAKE_SURE_THREAD(type, method, ...)                         \
  if (TaskRunner::GetTaskRunner(RunnerType::type) !=                \
      TaskRunner::GetTaskRunner(RunnerType::Self)) {                \
    RUN_ON_THREAD(type, method, ##__VA_ARGS__)                      \
    return;                                                         \
  }

#define RUN_ON_IO_THREAD(method, ...)                               \
  RUN_ON_THREAD(Io, method, ##__VA_ARGS__)

#define MAKE_SURE_IO_THREAD(method, ...)                            \
  MAKE_SURE_THREAD(Io, method, ##__VA_ARGS__)

#define RUN_ON_MAIN_THREAD(method, ...)                             \
  RUN_ON_THREAD(Main, method, ##__VA_ARGS__)

#define MAKE_SURE_MAIN_THREAD(method, ...)                          \
  MAKE_SURE_THREAD(Main, method, ##__VA_ARGS__)

using ClientConnectCB = std::function<void(int id)>;
using ClientMessageCB = std::function<void(int id, const std::string)>;

class IoManager {
public:
  void StartServer(ClientConnectCB connect_cb, ClientConnectCB disconnect_cb,
                                                 ClientMessageCB client_msg_cb);

  void SendMessageToClient(int client_fd, std::string msg);

  void SendControlMessage();

private:
  static constexpr char SERVER_SOCK_FILE[] = "/tmp/request_server.sock";

  // Resources owned by main thread
  int control_fd_ = -1;

  // Resources owned by IO thread
  ClientConnectCB client_connected_cb_;
  ClientConnectCB client_disconnected_cb_;
  ClientMessageCB client_msg_recvd_cb_;

  std::unordered_map<int, std::function<void(int)>> listen_fd_cb_map_;
  
  //Request Server started before the ListenLoop. No control message needed.
  void StartServerSocket();

  void AcceptConnection(int server_fd);

  void OnReceiveSocketEvent(int client_fd);

  void RegisterControlFD(int ctrl_fd);

  void OnControlMessage(int ctrl_fd);

  inline void PostListenLoopTask();

  void SendMessageToClientInternal(int client_fd, std::string msg);

  void ListenLoop();
};

#endif //IO_MANAGER_H_
