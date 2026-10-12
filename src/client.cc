#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

int main() {
  int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (clientSocket == -1) {
    std::cerr << "Failed to create socket. errno: " << errno << std::endl;
    return 1;
  }

  sockaddr_in serverAddress{};
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(9090);

  if (inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr) <= 0) {
    std::cerr << "Invalid address or address not supported" << std::endl;
    ::close(clientSocket);
    return 1;
  }

  if (connect(clientSocket, (struct sockaddr *)&serverAddress,
              sizeof(serverAddress)) < 0) {
    std::cerr << "Connection failed. errno: " << errno << std::endl;
    ::close(clientSocket);
    return 1;
  }

  std::cout << "Connected to the server successfully!" << std::endl;

  constexpr std::string_view message = "Hello from client!";
  ::send(clientSocket, message.data(), message.size(), 0);

  ::close(clientSocket);
  return 0;
}
