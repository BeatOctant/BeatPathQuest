#pragma once

#include "octantpath/core/transport.hpp"

#include <optional>

namespace octantpath::core {

// POSIX のソケットで平文の HTTP/1.1 を 1 回ずつ送る Transport（契約6: 当面は LAN でも平文の HTTP）。
// Quest（Android）でも PC でも同じに動く。https は扱わない
class PosixTransport : public Transport {
public:
  HttpResponse send(Endpoint const &endpoint, HttpRequest const &request, std::chrono::milliseconds timeout) override;
};

// 受け取った応答（状態行・ヘッダ・本文）を読む。chunked なら本文をつなぐ。読めなければ nullopt
struct ParsedHttpResponse {
  int status = 0;
  std::string body;
};
std::optional<ParsedHttpResponse> parseHttpResponse(std::string const &raw);

} // namespace octantpath::core
