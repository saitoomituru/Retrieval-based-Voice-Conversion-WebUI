#pragma once

#include <string>
#include <vector>

namespace rvc {

struct RuntimeChoices {
  std::vector<std::string> choices;
  std::string selected;
  std::string error;
};

class RuntimeControlClient {
public:
  RuntimeChoices list() const;
  bool select(const std::string& choice, std::string& error) const;
};

} // namespace rvc
