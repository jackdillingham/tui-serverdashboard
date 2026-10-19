#include "kubernetes_client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "process.hpp"

namespace {

std::vector<std::string> Split(const std::string& text, char delimiter) {
  std::vector<std::string> fields;
  std::istringstream stream(text);
  std::string field;
  while (std::getline(stream, field, delimiter)) {
    fields.push_back(field);
  }
  return fields;
}

std::vector<std::string> SplitWhitespace(const std::string& text) {
  std::istringstream stream(text);
  std::vector<std::string> fields;
  std::string field;
  while (stream >> field) {
    fields.push_back(field);
  }
  return fields;
}

double ParseCpu(const std::string& value) {
  try {
    if (!value.empty() && value.back() == 'm') {
      return std::stod(value.substr(0, value.size() - 1)) / 1000.0;
    }
    return std::stod(value);
  } catch (const std::exception&) {
    return -1;
  }
}

double ParseMemoryMiB(const std::string& value) {
  try {
    const double amount = std::stod(value);
    if (value.find("Ki") != std::string::npos) return amount / 1024.0;
    if (value.find("Gi") != std::string::npos) return amount * 1024.0;
    if (value.find("Ti") != std::string::npos) return amount * 1024.0 * 1024.0;
    if (value.find("Mi") != std::string::npos) return amount;
    if (value.find('K') != std::string::npos) return amount / 1024.0;
    if (value.find('G') != std::string::npos) return amount * 1024.0;
    if (value.find('T') != std::string::npos) return amount * 1024.0 * 1024.0;
    return amount / (1024.0 * 1024.0);
  } catch (const std::exception&) {
    return -1;
  }
}

double Percent(double used, double capacity) {
  if (used < 0 || capacity <= 0) return -1;
  return std::clamp(used * 100.0 / capacity, 0.0, 100.0);
}

bool IsSafeTarget(const std::string& target) {
  return !target.empty() &&
         target.find_first_not_of(
             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@") ==
             std::string::npos;
}

std::map<std::string, std::string> ParseKeyValues(const std::string& output) {
  std::map<std::string, std::string> values;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    const std::size_t separator = line.find('=');
    if (separator != std::string::npos) {
      values[line.substr(0, separator)] = line.substr(separator + 1);
    }
  }
  return values;
}

double ParseNumber(const std::map<std::string, std::string>& values,
                   const std::string& key) {
  const auto found = values.find(key);
  if (found == values.end()) return -1;
  try {
    return std::stod(found->second);
  } catch (const std::exception&) {
    return -1;
  }
}

std::string GetValue(const std::map<std::string, std::string>& values,
                     const std::string& key) {
  const auto found = values.find(key);
  return found == values.end() ? std::string() : found->second;
}

std::vector<std::string> SshArguments(const std::string& target,
                                      const std::string& remote_command) {
  return {"ssh", "-o", "ConnectTimeout=4", "-o", "BatchMode=yes", target,
          remote_command};
}

}  // namespace

bool KubernetesClient::Refresh(std::vector<Node>& nodes,
                               std::string& error) {
  const auto previous_nodes = nodes;
  const auto now = std::chrono::steady_clock::now();
  const double elapsed =
      last_refresh_.time_since_epoch().count() == 0
          ? 0
          : std::chrono::duration<double>(now - last_refresh_).count();
  std::vector<Node> refreshed;
  if (!ReadNodes(refreshed, error)) return false;
  std::string metrics_message;
  ReadUtilization(refreshed, metrics_message);

  std::atomic<std::size_t> next_node{0};
  const std::size_t worker_count = std::min<std::size_t>(8, refreshed.size());
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  std::vector<std::string> detail_errors(refreshed.size());
  for (std::size_t worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&] {
      std::size_t index = 0;
      while ((index = next_node.fetch_add(1)) < refreshed.size()) {
        detail_errors[index] = ReadNodeDetails(refreshed[index]);
        refreshed[index].ssh_error = detail_errors[index];
      }
    });
  }
  for (std::thread& worker : workers) worker.join();

  std::size_t unavailable = 0;
  for (std::size_t i = 0; i < refreshed.size(); ++i) {
    if (!detail_errors[i].empty()) ++unavailable;
    const auto previous = std::find_if(
        previous_nodes.begin(), previous_nodes.end(),
        [&](const Node& node) { return node.name == refreshed[i].name; });
    if (previous == previous_nodes.end() || elapsed <= 0) continue;
    const double received =
        refreshed[i].stats.receive_bytes - previous->stats.receive_bytes;
    const double transmitted =
        refreshed[i].stats.transmit_bytes - previous->stats.transmit_bytes;
    if (received >= 0) {
      refreshed[i].stats.receive_bytes_per_second = received / elapsed;
    }
    if (transmitted >= 0) {
      refreshed[i].stats.transmit_bytes_per_second = transmitted / elapsed;
    }
  }
  nodes = std::move(refreshed);
  last_refresh_ = now;
  error = metrics_message;
  if (unavailable > 0) {
    if (!error.empty()) error += " ";
    error += std::to_string(unavailable) +
             " node(s) could not be reached over SSH.";
  }
  return true;
}

bool KubernetesClient::ReadNodes(std::vector<Node>& nodes,
                                 std::string& error) const {
  const std::string json_path =
      "{range .items[*]}{.metadata.name}{\"\\t\"}"
      "{.status.conditions[?(@.type==\"Ready\")].status}{\"\\t\"}"
      "{.status.addresses[?(@.type==\"InternalIP\")].address}{\"\\t\"}"
      "{.metadata.annotations['serverdash.io/mac-address']}{\"\\t\"}"
      "{.status.capacity.cpu}{\"\\t\"}{.status.capacity.memory}{\"\\n\"}{end}";
  const CommandResult result = Process::Run(
      {"kubectl", "get", "nodes", "-o", "jsonpath=" + json_path});
  if (result.exit_code != 0) {
    error = "kubectl could not list nodes: " + result.output;
    return false;
  }

  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    const std::vector<std::string> fields = Split(line, '\t');
    if (fields.size() < 6 || fields[0].empty()) continue;
    Node node;
    node.name = fields[0];
    node.ready = fields[1];
    node.internal_ip = fields[2];
    node.mac_address = fields[3];
    node.cpu_capacity = fields[4];
    node.memory_capacity = fields[5];
    nodes.push_back(std::move(node));
  }
  if (nodes.empty()) {
    error = "The active Kubernetes context returned no nodes.";
    return false;
  }
  return true;
}

void KubernetesClient::ReadUtilization(std::vector<Node>& nodes,
                                       std::string& message) const {
  const CommandResult result =
      Process::Run({"kubectl", "top", "nodes", "--no-headers"});
  if (result.exit_code != 0) {
    message =
        "CPU/memory utilization unavailable; install metrics-server. "
        "Node details are still shown.";
    return;
  }

  std::istringstream stream(result.output);
  std::string line;
  while (std::getline(stream, line)) {
    const std::vector<std::string> fields = SplitWhitespace(line);
    if (fields.size() < 5) continue;
    const auto found = std::find_if(nodes.begin(), nodes.end(),
                                    [&](const Node& node) {
                                      return node.name == fields[0];
                                    });
    if (found == nodes.end()) continue;

    const double cpu_used = ParseCpu(fields[1]);
    const double cpu_capacity = ParseCpu(found->cpu_capacity);
    const double memory_used = ParseMemoryMiB(fields[3]);
    const double memory_capacity = ParseMemoryMiB(found->memory_capacity);
    found->stats.cpu_percent = Percent(cpu_used, cpu_capacity);
    found->stats.memory_percent = Percent(memory_used, memory_capacity);
  }
}

std::string KubernetesClient::ReadNodeDetails(Node& node) const {
  const std::string target = SshTarget(node);
  if (!IsSafeTarget(target)) return "Invalid SSH target for node " + node.name;

  const std::string script =
      "printf 'uptime_seconds=%s\\n' \"$(cut -d. -f1 /proc/uptime)\"; "
      "for f in /sys/class/thermal/thermal_zone*/temp; do "
      "[ -r \"$f\" ] || continue; t=$(cat \"$f\"); "
      "case \"$t\" in *[!0-9]*|'') continue;; esac; "
      "awk -v t=\"$t\" 'BEGIN { printf \"temperature_celsius=%.1f\\n\", "
      "t > 1000 ? t / 1000 : t }'; break; done; "
      "df -P / | awk 'NR==2 {printf \"disk_used_percent=%s\\ndisk_available=%s\\n\", "
      "substr($5,1,length($5)-1),$4}'; "
      "iface=$(ip route show default 2>/dev/null | awk 'NR==1 {print $5}'); "
      "if [ -n \"$iface\" ]; then "
      "printf 'interface_name=%s\\n' \"$iface\"; "
      "awk -v i=\"$iface:\" '$1==i {printf \"receive_bytes=%s\\ntransmit_bytes=%s\\n\", "
      "$2,$10}' /proc/net/dev; "
      "cat \"/sys/class/net/$iface/speed\" 2>/dev/null | "
      "awk '{if ($1 > 0) printf \"link_speed=%s Mbps\\n\", $1}'; "
      "ip -o -4 addr show dev \"$iface\" 2>/dev/null | "
      "awk 'NR==1 {print \"internal_ip=\" $4}' | cut -d/ -f1; "
      "fi; "
      "ping -c 1 -W 2 1.1.1.1 2>/dev/null | "
      "awk -F'time=' '/time=/ {split($2,a,\" \"); print \"ping_ms=\" a[1]; exit}'";

  const CommandResult result = Process::Run(SshArguments(target, script));
  if (result.exit_code != 0) {
    return "SSH unavailable; check SSH config/keys and node reachability.";
  }

  const auto values = ParseKeyValues(result.output);
  node.stats.uptime_seconds = ParseNumber(values, "uptime_seconds");
  node.stats.temperature_celsius = ParseNumber(values, "temperature_celsius");
  node.stats.disk_used_percent = ParseNumber(values, "disk_used_percent");
  node.stats.ping_ms = ParseNumber(values, "ping_ms");
  node.stats.receive_bytes = ParseNumber(values, "receive_bytes");
  node.stats.transmit_bytes = ParseNumber(values, "transmit_bytes");
  node.stats.disk_available = GetValue(values, "disk_available");
  node.stats.internal_ip =
      GetValue(values, "internal_ip").empty() ? node.internal_ip
                                               : GetValue(values, "internal_ip");
  node.stats.interface_name = GetValue(values, "interface_name");
  node.stats.link_speed = GetValue(values, "link_speed");
  return {};
}

std::string KubernetesClient::RunNodeAction(const Node& node,
                                            const std::string& action) const {
  if (action != "shutdown" && action != "reboot") {
    return "Unsupported node action.";
  }
  const std::string target = SshTarget(node);
  if (!IsSafeTarget(target)) return "Invalid SSH target for node.";

  const std::string remote_command =
      action == "shutdown" ? "sudo -n shutdown -h now" : "sudo -n reboot";
  const CommandResult result = Process::Run(SshArguments(target, remote_command));
  if (result.exit_code != 0) {
    return action + " failed: " + result.output;
  }
  return action == "shutdown" ? "Shutdown command sent to " + node.name + "."
                              : "Reboot command sent to " + node.name + ".";
}

std::string KubernetesClient::WakeNode(const Node& node) const {
  if (node.mac_address.empty()) {
    return "No MAC address configured. Add the "
           "serverdash.io/mac-address node annotation.";
  }
  bool valid_mac = node.mac_address.size() == 17;
  for (std::size_t i = 0; valid_mac && i < node.mac_address.size(); ++i) {
    if (i % 3 == 2) {
      valid_mac = node.mac_address[i] == ':';
    } else {
      valid_mac = std::isxdigit(
          static_cast<unsigned char>(node.mac_address[i])) != 0;
    }
  }
  if (!valid_mac) {
    return "Invalid MAC address in serverdash.io/mac-address annotation.";
  }
  const CommandResult result =
      Process::Run({"wakeonlan", node.mac_address});
  if (result.exit_code != 0) {
    return "Wake-on-LAN failed. Install the wakeonlan utility on the dashboard "
           "host and check the node MAC address.";
  }
  return "Wake-on-LAN packet sent to " + node.name + ".";
}

std::string KubernetesClient::SshTarget(const Node& node) const {
  const char* user = std::getenv("SERVERDASH_SSH_USER");
  const std::string target = user != nullptr && user[0] != '\0'
                                 ? std::string(user) + "@" + node.name
                                 : node.name;
  return target;
}
