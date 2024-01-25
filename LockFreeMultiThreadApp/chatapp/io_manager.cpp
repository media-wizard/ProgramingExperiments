#include "io_manager.h"
#include "task_runner.h"

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <fcntl.h>

#include <functional>
#include <iostream>
#include <memory>

void IoManager::StartServer(ClientConnectCB connect_cb, ClientConnectCB disconnect_cb,
                                                ClientMessageCB client_msg_cb) {

  client_connected_cb_ = std::move(connect_cb);
  client_disconnected_cb_ = std::move(disconnect_cb);
  client_msg_recvd_cb_  = std::move(client_msg_cb);

  int ctrl_pipe[2] = {-1};
  if (pipe(ctrl_pipe) < 0) {
      std::cout << __func__ << ": IoManager: Failed to create control pipe.\n";
      return;
  }

  control_fd_ = ctrl_pipe[1];
  StartServerSocket();
  RegisterControlFD(ctrl_pipe[0]);
}

void IoManager::SendMessageToClient(int client_fd, std::string msg) {
  SendMessageToClientInternal(client_fd, msg);
  SendControlMessage();
}

void IoManager::SendMessageToClientInternal(int client_fd, std::string msg) {
  MAKE_SURE_IO_THREAD(&IoManager::SendMessageToClient, client_fd, msg)

  send(client_fd, msg.c_str(), msg.length(), 0);
}


void IoManager::SendControlMessage() { // Sending control message from main thread
  static constexpr char NEW_TASK[] = "NewTask";

  ssize_t rc = write(control_fd_, NEW_TASK, sizeof(NEW_TASK));
  if (rc < 0) {
    std::cout << __func__
              << ": Sending control message failed: "
              << strerror(errno) <<std::endl;
  }
}

void IoManager::StartServerSocket() {
  MAKE_SURE_IO_THREAD(&IoManager::StartServerSocket)

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    std::cout << __func__ << ": Request Server start failed.\n";
  } else {
    struct sockaddr_un addr;

    fcntl(sock, F_SETFL, O_NONBLOCK); // Set socket as non-blocking
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SERVER_SOCK_FILE);
    unlink(SERVER_SOCK_FILE);

    size_t len = sizeof(addr);
    if (bind(sock, (struct sockaddr *)&addr, len) != 0 ||
                                                      listen(sock, 2) != 0) {
      std::cout << __func__ << ": Error binding/listening Server socket.\n";
      close(sock);
    } else {
      std::cout << __func__ << ": Request Server started.\n";
      listen_fd_cb_map_[sock] = std::move(std::bind(&IoManager::AcceptConnection, this, std::placeholders::_1));
    }
  }
}

void IoManager::AcceptConnection(int server_fd) {
  int client_fd = -1;
  do {
    //Accept all connections
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno != EWOULDBLOCK) {
          std::cout << __func__ << ": Accept failed.\n";
        }
        break;
    }
    listen_fd_cb_map_[client_fd] =
        std::move(std::bind(&IoManager::OnReceiveSocketEvent, this, std::placeholders::_1));
    client_connected_cb_(client_fd);
  } while(client_fd >= 0);
}

void IoManager::OnReceiveSocketEvent(int client_fd) {
  ssize_t rc = -1;
  do {
    char buf[1024] = {0};
    ssize_t rc = recv(client_fd, buf, sizeof(buf), 0);
    if (rc <= 0) {
        std::cout << __func__ << ": Error on client socket : " << client_fd << std::endl;
        listen_fd_cb_map_.erase(client_fd);
        close(client_fd);
        client_disconnected_cb_(client_fd);
        break;
    } else {
        client_msg_recvd_cb_(client_fd, buf);
    }
  } while(rc >= 1024);
}

void IoManager::RegisterControlFD(int ctrl_fd) {
  MAKE_SURE_IO_THREAD(&IoManager::RegisterControlFD, ctrl_fd);

  listen_fd_cb_map_[ctrl_fd] = std::move(
      std::bind(&IoManager::OnControlMessage, this, std::placeholders::_1));

  std::cout << __func__ << ": ControlFD[" << ctrl_fd << "] registered.\n";
  //Control channel ready. Start listening
  PostListenLoopTask();
}

void IoManager::OnControlMessage(int ctrl_fd) {
  char buf[128] = {0};
  read(ctrl_fd, buf, sizeof(buf)); //Just clearing the pipe 
}

inline void IoManager::PostListenLoopTask() {
  RUN_ON_IO_THREAD(&IoManager::ListenLoop)
}
  
void IoManager::ListenLoop() {
  fd_set read_fds;
  int max_fd = -1;
  FD_ZERO(&read_fds);

  for (auto fd_cb = listen_fd_cb_map_.begin();
                          fd_cb != listen_fd_cb_map_.end(); fd_cb++) {
      if(fd_cb->first > -1)
        FD_SET(fd_cb->first, &read_fds);

      if (fd_cb->first > max_fd)
        max_fd = fd_cb->first;
  }

  // wait for an activity on one of the sockets , timeout is NULL ,
  // Control pipe will trigger a break.
  int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
  if ((activity < 0) && (errno!=EINTR)) {   
      std::cout << __func__ << ": Select error.\n";
  } else {
    auto fd_cb = listen_fd_cb_map_.begin();
    do {
      for (fd_cb = listen_fd_cb_map_.begin();
                          fd_cb != listen_fd_cb_map_.end(); fd_cb++) {
        int fd = fd_cb->first;
        if (FD_ISSET(fd, &read_fds)) {
          FD_CLR(fd, &read_fds);
          (fd_cb->second)(fd);
          break; //Safe to break: Map entry would erase in callback function.
        }
      }
    } while (fd_cb != listen_fd_cb_map_.end());
  }

  //Restart ListenLoop after finishing read operations
  PostListenLoopTask();
}
