#pragma once

#include <string>
#include <vector>

#include "node.hpp"

class Process {
 public:
  static CommandResult Run(const std::vector<std::string>& arguments);
  static int RunInteractive(const std::vector<std::string>& arguments);
};
