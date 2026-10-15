#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string_view>

int main() {
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd == -1) {
    std::cerr << "Failed to create socket. errno: " << errno << std::endl;
    return 1;
  }

  sockaddr_in server_address{};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(9090);

  if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) {
    std::cerr << "Invalid address or address not supported" << std::endl;
    ::close(client_fd);
    return 1;
  }

  if (connect(client_fd, (struct sockaddr *)&server_address,
              sizeof(server_address)) < 0) {
    std::cerr << "Connection failed. errno: " << errno << std::endl;
    ::close(client_fd);
    return 1;
  }

  std::cout << "Connected to the server successfully!" << std::endl;

  constexpr std::string_view message = "Hello from client!";
  ::send(client_fd, message.data(), message.size(), 0);
  shutdown(client_fd, SHUT_WR); // Indicate writing finished

  char buffer[1];
  while (true) {
    ssize_t bytes_received = ::recv(client_fd, buffer, sizeof(buffer), 0);
    if (bytes_received > 0) {
      std::cout << std::string_view(buffer, bytes_received) << std::flush;
    } else if (bytes_received == -1 && errno == EINTR) {
      continue;
    } else {
      // 0 = server closed after flushing its reply; -1 = error
      if (bytes_received == -1) {
        std::cerr << "recv failed. errno: " << errno << std::endl;
      }

      break;
    }
  }

  std::cout << std::endl;

  ::close(client_fd);
  return 0;
}
