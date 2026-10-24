#pragma once

#include "octantpath/core/transport.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// 決めた応答を返す Transport。受け取った要求を残す
struct FakeTransport : octantpath::core::Transport {
  std::map<std::string, octantpath::core::HttpResponse> responses;
  std::vector<std::pair<octantpath::core::Endpoint, octantpath::core::HttpRequest>> requests;

  octantpath::core::HttpResponse send(octantpath::core::Endpoint const &endpoint, octantpath::core::HttpRequest const &request, std::chrono::milliseconds) override {
    requests.emplace_back(endpoint, request);
    auto it = responses.find(request.method + " " + request.path);
    if (it == responses.end()) return {.reached = false, .error = "connect failed: Connection refused"};
    return it->second;
  }

  static octantpath::core::HttpResponse ok(std::string body, int status = 200) { return {.reached = true, .status = status, .body = std::move(body)}; }
};

// OctantSpecs の contracts/examples/mod-api/ の例を読む
inline std::string example(std::string const &name) {
  std::ifstream file(std::string(OCTANTSPECS_DIR) + "/contracts/examples/mod-api/" + name);
  if (!file) throw std::runtime_error("example not found (run scripts/fetch-octantspecs.sh): " + name);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

inline std::string header(octantpath::core::HttpRequest const &request, std::string const &name) {
  for (auto const &[key, value] : request.headers)
    if (key == name) return value;
  return "";
}
