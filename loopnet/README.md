# loopnet

A small single-threaded, reactor-style TCP server library built on `poll(2)`.

## Usage

```cpp
#include "loopnet/event_loop.h"
#include "loopnet/tcp_connection.h"
#include "loopnet/tcp_server.h"

int main() {
  loopnet::EventLoop loop;
  loopnet::TcpServer server{loop, "127.0.0.1", 9090};

  server.set_message_callback(
      [](const loopnet::TcpConnectionPtr &conn, loopnet::Buffer &buf) {
        conn->send(buf.retrieve(buf.readable_bytes())); // echo
      });

  server.start();
  loop.start(); // blocks until loop.stop()
}
```

## Demo

`loopnet_server` listens on `127.0.0.1:9090`, prints each message it receives and replies with `ACK`:

```sh
./build/loopnet/loopnet_server
echo hello | nc 127.0.0.1 9090
```
