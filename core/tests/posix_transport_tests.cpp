#include "octantpath/core/posix_transport.hpp"

#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace octantpath::core;

namespace {

// 1 回だけ要求を受けて、決めた応答を返すサーバー
struct OneShotServer {
  int fd = -1;
  int port = 0;
  std::string received;
  std::thread thread;

  explicit OneShotServer(std::string reply) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    socklen_t size = sizeof(address);
    getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size);
    port = ntohs(address.sin_port);
    listen(fd, 1);
    thread = std::thread([this, reply = std::move(reply)] {
      int client = accept(fd, nullptr, nullptr);
      char buffer[4096];
      while (received.find("\r\n\r\n") == std::string::npos) {
        auto read = recv(client, buffer, sizeof(buffer), 0);
        if (read <= 0) break;
        received.append(buffer, read);
      }
      auto length = received.find("Content-Length: ");
      if (length != std::string::npos) {
        auto expected = std::stoul(received.substr(length + 16));
        while (received.size() - received.find("\r\n\r\n") - 4 < expected) {
          auto read = recv(client, buffer, sizeof(buffer), 0);
          if (read <= 0) break;
          received.append(buffer, read);
        }
      }
      // 受け手が途中で切っても、テストのプロセスが SIGPIPE で落ちないようにする
      send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
      close(client);
    });
  }
  void wait() {
    if (thread.joinable()) thread.join();
  }
  ~OneShotServer() {
    wait();
    close(fd);
  }
};

} // namespace

TEST_CASE("平文の HTTP で送り、状態と本文を受け取る") {
  OneShotServer server("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\n\r\n{\"token\":\"abc\"}");
  PosixTransport transport;
  auto response = transport.send({"127.0.0.1", server.port}, {"POST", "/v1/pair", {{"OctantPath-Api-Version", "2.0"}}, R"({"secret":"1"})"}, std::chrono::seconds(5));
  CHECK(response.reached);
  CHECK(response.status == 200);
  CHECK(response.body == R"({"token":"abc"})");
  server.wait();
  CHECK(server.received.starts_with("POST /v1/pair HTTP/1.1\r\n"));
  CHECK(server.received.find("OctantPath-Api-Version: 2.0\r\n") != std::string::npos);
  CHECK(server.received.ends_with("\r\n\r\n{\"secret\":\"1\"}"));
}

TEST_CASE("chunked の本文をつなぐ") {
  auto parsed = parseHttpResponse("HTTP/1.1 401 Unauthorized\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n{\"a\"\r\n3\r\n:1}\r\n0\r\n\r\n");
  REQUIRE(parsed);
  CHECK(parsed->status == 401);
  CHECK(parsed->body == R"({"a":1})");
}

TEST_CASE("chunked の大きさが残りより大きいと読まない") {
  CHECK_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffffffffffffffff\r\nabc\r\n0\r\n\r\n"));
  CHECK_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nabc\r\n"));
  CHECK_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc"));
}

TEST_CASE("上限より大きい応答は読むのをやめる") {
  std::string body(200 * 1024, 'x');
  OneShotServer server("HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
  HttpRequest request{"GET", "/v1/local-zip/a", {}, {}};
  request.maxResponseBytes = 1024;
  auto response = PosixTransport{}.send({"127.0.0.1", server.port}, request, std::chrono::seconds(5));
  CHECK_FALSE(response.reached);
  CHECK(response.error == "the response is too large");
}

TEST_CASE("上限の少し上でも、本文が上限を超えれば受け取らない") {
  OneShotServer server("HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n01234567890123456789");
  HttpRequest request{"GET", "/v1/local-zip/a", {}, {}};
  request.maxResponseBytes = 10;
  auto response = PosixTransport{}.send({"127.0.0.1", server.port}, request, std::chrono::seconds(5));
  CHECK_FALSE(response.reached);
  CHECK(response.error == "the response is too large");
}

TEST_CASE("待ち受けていないポートには届かない") {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  socklen_t size = sizeof(address);
  getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size);
  int port = ntohs(address.sin_port);
  close(fd);
  auto response = PosixTransport{}.send({"127.0.0.1", port}, {"GET", "/v1/health", {}, {}}, std::chrono::seconds(2));
  CHECK_FALSE(response.reached);
  CHECK(response.error.find("connect failed") != std::string::npos);
}
