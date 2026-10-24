#include "octantpath/core/transport.hpp"

namespace octantpath::core {

std::string Endpoint::url() const {
  auto bracketed = host.find(':') != std::string::npos && !host.starts_with('[') ? "[" + host + "]" : host;
  return "http://" + bracketed + ":" + std::to_string(port) + "/";
}

} // namespace octantpath::core
