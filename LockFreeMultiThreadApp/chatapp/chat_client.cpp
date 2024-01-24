#include <cstdio>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <iostream>
#include <string>

static constexpr char SERVER_SOCK_FILE[] = "/tmp/request_server.sock";
static constexpr char CLIENT_SOCK_FILE[] = "/tmp/request_client.sock";
#define FD_CNT 2

void ListenLoop(int sock_fd) {
  bool exiting = false;
  int fd_array[FD_CNT] = {STDIN_FILENO, sock_fd};

  do {
		fd_set read_fds;
		int max_fd = -1;
		FD_ZERO(&read_fds);

		for (auto fd : fd_array) {
			FD_SET(fd, &read_fds);
			if (fd > max_fd)
				max_fd = fd;
		}

		// wait for an activity on one of the sockets , timeout is NULL ,
		// Control pipe will trigger a break.
		printf("\nMe : ");
		fflush(stdout);

		int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
		if ((activity < 0) && (errno!=EINTR)) {   
				std::cout << __func__ << ": Select error.\n";
		} else {
			if (FD_ISSET(STDIN_FILENO, &read_fds)) {
        std::string data;
        getline(std::cin, data);
        if (data == "exit") {
          exiting = true;
          break;
				}
        send(sock_fd, data.c_str(), data.length(), 0);
			}
			if (FD_ISSET(sock_fd, &read_fds)) {
				ssize_t rc = -1;
				do {
					char buf[1024] = {0};
					ssize_t rc = recv(sock_fd, buf, sizeof(buf), 0);
					if (rc <= 0) {
						std::cout << __func__ << ": Error on client socket. client_fd\n";
						exiting = true;
						break;
					} else if (rc > 0) {
						// printf("\nReceive %lu bytes\n", rc);
						buf[1023] = 0;
						printf("\n%s\n", buf);
					}
				} while(rc >= 1024);
			}
		}
	} while (!exiting);
}
  
int main() {
	int fd;
	struct sockaddr_un addr;
	int ret;
	char buff[8192];
	struct sockaddr_un from;
	int ok = 1;
	int len;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		ok = 0;
	}

	if (ok) {
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, CLIENT_SOCK_FILE);
		unlink(CLIENT_SOCK_FILE);
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("bind");
			ok = 0;
		}
	}

	if (ok) {
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, SERVER_SOCK_FILE);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			perror("connect");
			ok = 0;
		}
	}

	ListenLoop(fd);
        
	if (fd >= 0) {
		close(fd);
	}

	// unlink (CLIENT_SOCK_FILE);
	return 0;
}
