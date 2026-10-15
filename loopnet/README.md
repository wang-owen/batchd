# loopnet

A small single-threaded, reactor-style TCP server library in C++23, built on `poll(2)`.

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

## Build

Requires Meson ≥ 1.3 and a C++23 compiler.

```sh
meson setup build
meson compile -C build
```

This produces the static library `libloopnet.a` and the demo binary `loopnet_server`. Catch2 is fetched for tests by default; pass `-Dtests=false` to skip it.

## Run the demo

`loopnet_server` listens on `127.0.0.1:9090`, prints each message it receives and replies with `ACK`:

```sh
./build/loopnet_server
echo hello | nc 127.0.0.1 9090
```
