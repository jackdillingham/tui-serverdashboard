#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "node.hpp"

class KubernetesClient {
 public:
  bool Refresh(std::vector<Node>& nodes, std::string& error);
  std::string ReadNodeDetails(Node& node) const;
  std::string RunNodeAction(const Node& node, const std::string& action) const;
  std::string WakeNode(const Node& node) const;
  std::string SshTarget(const Node& node) const;

 private:
  bool ReadNodes(std::vector<Node>& nodes, std::string& error) const;
  void ReadUtilization(std::vector<Node>& nodes, std::string& message) const;
  std::chrono::steady_clock::time_point last_refresh_{};
};
