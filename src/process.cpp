#include "process.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#endif

namespace {

std::string QuoteArgument(const std::string& argument) {
#ifdef _WIN32
  std::string quoted = "\"";
  for (char character : argument) {
    if (character == '"') {
      quoted += "\\\"";
    } else if (character == '%') {
      quoted += "%%";
    } else {
      quoted += character;
    }
  }
  quoted += '"';
  return quoted;
#else
  std::string quoted = "'";
  for (char character : argument) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted += character;
    }
  }
  quoted += '\'';
  return quoted;
#endif
}

std::string BuildCommand(const std::vector<std::string>& arguments) {
  std::string command;
  for (const std::string& argument : arguments) {
    if (!command.empty()) {
      command += ' ';
    }
    command += QuoteArgument(argument);
  }
  return command;
}

}  // namespace

CommandResult Process::Run(const std::vector<std::string>& arguments) {
  CommandResult result;
  const std::string command = BuildCommand(arguments) + " 2>&1";
#ifdef _WIN32
  FILE* pipe = _popen(command.c_str(), "r");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    result.output = "Could not start command.";
    return result;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result.output += buffer;
  }
#ifdef _WIN32
  result.exit_code = _pclose(pipe);
#else
  const int status = pclose(pipe);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
  return result;
}

int Process::RunInteractive(const std::vector<std::string>& arguments) {
  return std::system(BuildCommand(arguments).c_str());
}
