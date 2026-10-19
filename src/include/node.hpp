#pragma once

#include <string>
#include <vector>

struct NodeStats {
  double cpu_percent = -1;
  double memory_percent = -1;
  double uptime_seconds = -1;
  double temperature_celsius = -1;
  double disk_used_percent = -1;
  std::string disk_available;
  std::string internal_ip;
  std::string interface_name;
  std::string link_speed;
  double ping_ms = -1;
  double receive_bytes = -1;
  double transmit_bytes = -1;
  double receive_bytes_per_second = -1;
  double transmit_bytes_per_second = -1;
};

struct Node {
  std::string name;
  std::string ready;
  std::string internal_ip;
  std::string mac_address;
  std::string cpu_capacity;
  std::string memory_capacity;
  std::string ssh_error;
  NodeStats stats;
};

struct CommandResult {
  int exit_code = -1;
  std::string output;
};
