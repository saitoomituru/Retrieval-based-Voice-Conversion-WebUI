#include "RuntimeControlClient.hpp"

#if !defined(__APPLE__)
#error RuntimeControlClient_mac.cpp is for the macOS AU thin head.
#endif

#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr uint16_t kControlPort = 17864;
constexpr size_t kMaxResponseBytes = 64 * 1024;

std::string percentEncode(const std::string& input)
{
  constexpr char hex[] = "0123456789ABCDEF";
  std::string output;
  for (const unsigned char value : input) {
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '-' || value == '_'
        || value == '.' || value == '~') {
      output.push_back(static_cast<char>(value));
    } else {
      output.push_back('%');
      output.push_back(hex[value >> 4]);
      output.push_back(hex[value & 0x0f]);
    }
  }
  return output;
}

int hexValue(const char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool percentDecode(const std::string& input, std::string& output)
{
  output.clear();
  for (size_t index = 0; index < input.size(); ++index) {
    if (input[index] != '%') {
      output.push_back(input[index]);
      continue;
    }
    if (index + 2 >= input.size())
      return false;
    const int high = hexValue(input[index + 1]);
    const int low = hexValue(input[index + 2]);
    if (high < 0 || low < 0)
      return false;
    output.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }
  return true;
}

bool request(const char* method, const char* path, const std::string& body,
             std::string& responseBody, std::string& error)
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    error = "runtime control socket failed";
    return false;
  }
  timeval timeout {0, 350000};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(kControlPort);
  ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    error = "WebUI runtime control unavailable at 127.0.0.1:17864";
    ::close(fd);
    return false;
  }

  std::string wire = std::string(method) + " " + path + " HTTP/1.0\r\n"
    + "Host: 127.0.0.1\r\nConnection: close\r\n";
  if (!body.empty()) {
    wire += "Content-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  }
  wire += "\r\n" + body;
  size_t sent = 0;
  while (sent < wire.size()) {
    const ssize_t count = ::send(fd, wire.data() + sent, wire.size() - sent, 0);
    if (count <= 0) {
      error = "runtime control request failed";
      ::close(fd);
      return false;
    }
    sent += static_cast<size_t>(count);
  }

  std::string response;
  char buffer[4096];
  while (response.size() < kMaxResponseBytes) {
    const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
    if (count == 0)
      break;
    if (count < 0) {
      error = errno == EAGAIN ? "runtime control response timed out" : "runtime control response failed";
      ::close(fd);
      return false;
    }
    response.append(buffer, static_cast<size_t>(count));
  }
  ::close(fd);
  if (response.rfind("HTTP/1.0 200", 0) != 0 && response.rfind("HTTP/1.1 200", 0) != 0) {
    error = "runtime control rejected request";
    return false;
  }
  const size_t separator = response.find("\r\n\r\n");
  if (separator == std::string::npos) {
    error = "invalid runtime control response";
    return false;
  }
  responseBody = response.substr(separator + 4);
  return true;
}

} // namespace

namespace rvc {

RuntimeChoices RuntimeControlClient::list() const
{
  RuntimeChoices result;
  std::string body;
  if (!request("GET", "/v1/runtimes.txt", {}, body, result.error))
    return result;
  size_t start = 0;
  bool headerSeen = false;
  while (start <= body.size()) {
    const size_t end = body.find('\n', start);
    std::string line = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!headerSeen) {
      if (line != "RSVC-CONTROL/1") {
        result.error = "unsupported runtime control protocol";
        return result;
      }
      headerSeen = true;
    } else {
      const size_t tab = line.find('\t');
      if (tab != std::string::npos) {
        std::string decoded;
        if (!percentDecode(line.substr(tab + 1), decoded)) {
          result.error = "invalid runtime name encoding";
          return result;
        }
        if (line.compare(0, tab, "selected") == 0)
          result.selected = decoded;
        else if (line.compare(0, tab, "model") == 0)
          result.model = decoded;
        else if (line.compare(0, tab, "index") == 0)
          result.index = decoded;
        else if (line.compare(0, tab, "choice") == 0)
          result.choices.push_back(decoded);
      }
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  if (!headerSeen || result.choices.empty())
    result.error = "runtime control returned no engines";
  return result;
}

bool RuntimeControlClient::select(const std::string& choice, std::string& error) const
{
  std::string body;
  return request("POST", "/v1/select-text", percentEncode(choice), body, error)
      && body.rfind("OK", 0) == 0;
}

} // namespace rvc
