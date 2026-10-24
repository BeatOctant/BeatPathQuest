#include "octantpath/core/posix_transport.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>

namespace octantpath::core {

namespace {

struct Socket {
  int fd = -1;
  ~Socket() {
    if (fd >= 0) close(fd);
  }
};

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}

// 期限までにつなぐ。つながらなければ理由を返す
std::optional<std::string> connectWithin(int fd, sockaddr const *address, socklen_t length, std::chrono::milliseconds timeout) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (connect(fd, address, length) < 0) {
    if (errno != EINPROGRESS) return std::string("connect failed: ") + std::strerror(errno);
    pollfd waiting{fd, POLLOUT, 0};
    int ready = poll(&waiting, 1, static_cast<int>(timeout.count()));
    if (ready == 0) return std::string("connect timed out");
    if (ready < 0) return std::string("connect failed: ") + std::strerror(errno);
    int error = 0;
    socklen_t size = sizeof(error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size);
    if (error != 0) return std::string("connect failed: ") + std::strerror(error);
  }
  fcntl(fd, F_SETFL, flags);
  return std::nullopt;
}

} // namespace

std::optional<ParsedHttpResponse> parseHttpResponse(std::string const &raw) {
  auto headerEnd = raw.find("\r\n\r\n");
  if (!raw.starts_with("HTTP/1.") || headerEnd == std::string::npos || raw.size() < 12) return std::nullopt;
  ParsedHttpResponse parsed;
  try {
    parsed.status = std::stoi(raw.substr(9, 3));
  } catch (std::exception const &) {
    return std::nullopt;
  }
  auto headers = lower(raw.substr(0, headerEnd));
  auto body = raw.substr(headerEnd + 4);
  if (headers.find("\r\ntransfer-encoding: chunked") == std::string::npos) {
    parsed.body = std::move(body);
    return parsed;
  }
  size_t at = 0;
  while (true) {
    auto lineEnd = body.find("\r\n", at);
    if (lineEnd == std::string::npos) return std::nullopt;
    size_t size = 0;
    try {
      size = std::stoul(body.substr(at, lineEnd - at), nullptr, 16);
    } catch (std::exception const &) {
      return std::nullopt;
    }
    if (size == 0) break;
    // 足し算は大きな size で桁あふれするので、残りの長さと比べる
    if (size > body.size() - (lineEnd + 2) || body.size() - (lineEnd + 2) - size < 2) return std::nullopt;
    parsed.body.append(body, lineEnd + 2, size);
    at = lineEnd + 2 + size + 2;
  }
  return parsed;
}

HttpResponse PosixTransport::send(Endpoint const &endpoint, HttpRequest const &request, std::chrono::milliseconds timeout) {
  HttpResponse response;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *found = nullptr;
  auto port = std::to_string(endpoint.port);
  if (int error = getaddrinfo(endpoint.host.c_str(), port.c_str(), &hints, &found); error != 0) {
    response.error = std::string("address could not be resolved: ") + gai_strerror(error);
    return response;
  }
  Socket socket;
  std::string failure = "no address";
  for (auto *address = found; address; address = address->ai_next) {
    Socket attempt;
    attempt.fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (attempt.fd < 0) {
      failure = std::string("socket failed: ") + std::strerror(errno);
      continue;
    }
    if (auto error = connectWithin(attempt.fd, address->ai_addr, address->ai_addrlen, timeout)) {
      failure = *error;
      continue;
    }
    std::swap(socket.fd, attempt.fd);
    break;
  }
  freeaddrinfo(found);
  if (socket.fd < 0) {
    response.error = failure;
    return response;
  }

  timeval wait{static_cast<time_t>(timeout.count() / 1000), static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
  setsockopt(socket.fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
  setsockopt(socket.fd, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));

  auto hostHeader = endpoint.host.find(':') != std::string::npos ? "[" + endpoint.host + "]" : endpoint.host;
  std::string message = request.method + " " + request.path + " HTTP/1.1\r\nHost: " + hostHeader + ":" + port + "\r\nConnection: close\r\nAccept: application/json\r\n";
  for (auto const &[name, value] : request.headers) message += name + ": " + value + "\r\n";
  if (!request.body.empty() || request.method == "POST") message += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
  message += "\r\n" + request.body;

  for (size_t sent = 0; sent < message.size();) {
    auto written = ::send(socket.fd, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
    if (written <= 0) {
      response.error = std::string("send failed: ") + std::strerror(errno);
      return response;
    }
    sent += static_cast<size_t>(written);
  }

  std::string raw;
  // 本文の上限に、ヘッダーと chunked の区切りの分の余白を足す
  auto maxRaw = request.maxResponseBytes + 64 * 1024;
  char buffer[8192];
  while (true) {
    auto read = recv(socket.fd, buffer, sizeof(buffer), 0);
    if (read == 0) break;
    if (read < 0) {
      response.error = errno == EAGAIN || errno == EWOULDBLOCK ? "timed out" : std::string("receive failed: ") + std::strerror(errno);
      return response;
    }
    raw.append(buffer, static_cast<size_t>(read));
    if (raw.size() > maxRaw) {
      response.error = "the response is too large";
      return response;
    }
  }
  auto parsed = parseHttpResponse(raw);
  if (!parsed) {
    response.error = raw.empty() ? "no response" : "the response is not HTTP";
    return response;
  }
  if (parsed->body.size() > request.maxResponseBytes) {
    response.error = "the response is too large";
    return response;
  }
  response.reached = true;
  response.status = parsed->status;
  response.body = std::move(parsed->body);
  return response;
}

} // namespace octantpath::core
