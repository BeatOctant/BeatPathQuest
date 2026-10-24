#pragma once

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace octantpath::core {

// つなぐ先（OctantCompass のアドレスとポート）
struct Endpoint {
  std::string host;
  int port = 0;

  // 人とログに見せる形（http://host:port/）。IPv6 は角括弧で囲む
  std::string url() const;
};

struct HttpRequest {
  std::string method;
  std::string path;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  // 受け取る本文の大きさの上限（バイト）。超えたら読むのをやめて失敗にする（悪意のある相手や壊れた相手にメモリを使い切らせない）
  size_t maxResponseBytes = 16 * 1024 * 1024;
};

struct HttpResponse {
  // 相手に届いて応答を受け取れたか。届かなければ status は 0 で、理由は error
  bool reached = false;
  int status = 0;
  std::string body;
  std::string error;
};

// HTTP を送る口。コアはゲームにも OS にも依存しないので、送り方は MOD（Quest ではソケット）とテストが差し込む
class Transport {
public:
  virtual ~Transport() = default;
  virtual HttpResponse send(Endpoint const &endpoint, HttpRequest const &request, std::chrono::milliseconds timeout) = 0;
};

} // namespace octantpath::core
