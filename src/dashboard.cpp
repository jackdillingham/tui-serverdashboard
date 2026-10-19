#include "dashboard.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/flexbox_config.hpp>

#include "process.hpp"

namespace {

using namespace ftxui;

const std::vector<std::string> kTabs{
    "Resources", "Network", "Storage", "Actions", "SSH"};

std::string FormatPercent(double value) {
  if (value < 0) return "N/A";
  std::ostringstream result;
  result << std::fixed << std::setprecision(1) << value << '%';
  return result.str();
}

std::string FormatTemperature(double value) {
  if (value < 0) return "N/A";
  std::ostringstream result;
  result << std::fixed << std::setprecision(1) << value << " C";
  return result.str();
}

std::string FormatBytes(double value) {
  if (value < 0) return "N/A";
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit = 0;
  while (value >= 1024 && unit < 4) {
    value /= 1024;
    ++unit;
  }
  std::ostringstream result;
  result << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' '
         << units[unit];
  return result.str();
}

std::string FormatRate(double value) {
  return value < 0 ? "N/A" : FormatBytes(value) + "/s";
}

std::string FormatUptime(double seconds) {
  if (seconds < 0) return "N/A";
  const long long total = static_cast<long long>(seconds);
  const long long days = total / 86400;
  const long long hours = (total % 86400) / 3600;
  const long long minutes = (total % 3600) / 60;
  std::ostringstream result;
  if (days > 0) result << days << "d ";
  result << hours << "h " << minutes << "m";
  return result.str();
}

Element Metric(const std::string& label, const std::string& value) {
  return hbox({text(label) | dim, filler(), text(value) | bold});
}

Element CardHeader(const ::Node& node) {
  const bool ready = node.ready == "True";
  return hbox({
      text(node.name) | bold | color(Color::Cyan),
      filler(),
      text(ready ? "READY" : (node.ready.empty() ? "UNKNOWN" : "NOT READY")) |
          (ready ? color(Color::Green) : color(Color::Red)),
  });
}

Element NodeCard(const ::Node& node, Elements metrics) {
  Elements content{CardHeader(node), separator()};
  content.insert(content.end(), metrics.begin(), metrics.end());
  if (!node.ssh_error.empty()) {
    content.push_back(text("SSH data unavailable") | color(Color::Red));
  }
  return vbox(std::move(content)) | border | flex;
}

std::string ToDisplayError(const std::string& text) {
  std::string result = text;
  std::replace(result.begin(), result.end(), '\n', ' ');
  return result;
}

}  // namespace

Dashboard::Dashboard(ftxui::App& app) : app_(app) {}

Dashboard::~Dashboard() {
  if (refresh_worker_.joinable()) refresh_worker_.join();
}

void Dashboard::Activate() {
  if (!loaded_) StartRefresh();
}

void Dashboard::StartRefresh() {
  if (refreshing_) return;
  if (refresh_worker_.joinable()) refresh_worker_.join();
  refreshing_ = true;
  message_ = "Refreshing Kubernetes and node data...";
  std::vector<::Node> previous_nodes = nodes_;
  refresh_worker_ = std::thread(
      [this, previous_nodes = std::move(previous_nodes)]() mutable {
        std::string error;
        const bool success = client_.Refresh(previous_nodes, error);
        app_.Post([this, nodes = std::move(previous_nodes), error = std::move(error),
                   success]() mutable {
          if (success) {
            nodes_ = std::move(nodes);
            selected_node_ = std::clamp(
                selected_node_, 0, static_cast<int>(nodes_.size()) - 1);
            RebuildNodeButtons();
          }
          message_ = ToDisplayError(error);
          if (message_.empty()) message_ = "Cluster refreshed.";
          loaded_ = true;
          refreshing_ = false;
        });
      });
}

void Dashboard::RebuildNodeButtons() {
  node_selection_container_->DetachAllChildren();
  node_buttons_.clear();
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    auto button = ftxui::Button(nodes_[i].name, [this, i] {
      SelectNode(static_cast<int>(i));
    });
    node_selection_container_->Add(button);
    node_buttons_.push_back(std::move(button));
  }
}

void Dashboard::SelectTab(int tab) {
  selected_tab_ = tab;
  pending_action_node_name_.clear();
  pending_action_.clear();
}

void Dashboard::SelectNode(int index) {
  if (nodes_.empty()) return;
  selected_node_ = (index + static_cast<int>(nodes_.size())) %
                   static_cast<int>(nodes_.size());
}

void Dashboard::RequestAction(const std::string& action) {
  if (nodes_.empty()) {
    message_ = "No Kubernetes nodes are available.";
    return;
  }
  pending_action_ = action;
  pending_action_node_name_ = nodes_[selected_node_].name;
}

void Dashboard::ConfirmAction() {
  const auto node = std::find_if(
      nodes_.begin(), nodes_.end(), [&](const ::Node& candidate) {
        return candidate.name == pending_action_node_name_;
      });
  if (node == nodes_.end()) {
    message_ = "The selected node is no longer in the cluster.";
    pending_action_node_name_.clear();
    pending_action_.clear();
    return;
  }
  message_ = pending_action_ == "wake"
                 ? client_.WakeNode(*node)
                 : client_.RunNodeAction(*node, pending_action_);
  pending_action_node_name_.clear();
  pending_action_.clear();
}

void Dashboard::ConnectSsh() {
  if (nodes_.empty()) {
    message_ = "No Kubernetes nodes are available.";
    return;
  }
  const std::string target = client_.SshTarget(nodes_[selected_node_]);
  if (target.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-@") !=
      std::string::npos) {
    message_ = "Invalid SSH target. Check the node name and SSH user.";
    return;
  }
  int result = -1;
  app_.WithRestoredIO([&] {
    result = Process::RunInteractive({"ssh", target});
  })();
  message_ = result == 0 ? "SSH session ended." : "SSH exited with an error.";
}

ftxui::Component Dashboard::Build() {
  using namespace ftxui;

  for (int i = 0; i < static_cast<int>(kTabs.size()); ++i) {
    tab_buttons_.push_back(Button(kTabs[i], [this, i] { SelectTab(i); }));
  }
  refresh_button_ =
      Button("Refresh cluster", [this] { StartRefresh(); });
  previous_node_button_ =
      Button("< Previous node", [this] { SelectNode(selected_node_ - 1); });
  next_node_button_ =
      Button("Next node >", [this] { SelectNode(selected_node_ + 1); });
  shutdown_button_ =
      Button("Shutdown", [this] { RequestAction("shutdown"); });
  reboot_button_ = Button("Reboot", [this] { RequestAction("reboot"); });
  wake_button_ = Button("Wake on LAN", [this] { RequestAction("wake"); });
  confirm_button_ = Button("Confirm", [this] { ConfirmAction(); });
  cancel_button_ = Button("Cancel", [this] {
    pending_action_node_name_.clear();
    pending_action_.clear();
  });
  ssh_button_ = Button("Open SSH terminal", [this] { ConnectSsh(); });

  auto tab_bar = Container::Horizontal(tab_buttons_);
  node_selection_container_ = Container::Horizontal({});
  auto footer = Container::Horizontal(
      {refresh_button_, previous_node_button_, next_node_button_,
       shutdown_button_, reboot_button_, wake_button_, confirm_button_,
       cancel_button_, ssh_button_});
  auto controls =
      Container::Vertical({tab_bar, node_selection_container_, footer});
  auto root = Renderer(controls, [this, tab_bar] {
    using namespace ftxui;
    Elements footer_controls{refresh_button_->Render()};
    if (selected_tab_ == 3 || selected_tab_ == 4) {
      footer_controls.push_back(previous_node_button_->Render());
      footer_controls.push_back(next_node_button_->Render());
    }
    if (selected_tab_ == 3 && pending_action_node_name_.empty()) {
      footer_controls.push_back(shutdown_button_->Render());
      footer_controls.push_back(reboot_button_->Render());
      footer_controls.push_back(wake_button_->Render());
    }
    if (selected_tab_ == 3 && !pending_action_node_name_.empty()) {
      footer_controls.push_back(confirm_button_->Render());
      footer_controls.push_back(cancel_button_->Render());
    }
    if (selected_tab_ == 4) footer_controls.push_back(ssh_button_->Render());

    Elements body;
    body.push_back(text("ServerDash  /  Kubernetes node overview") | bold);
    body.push_back(separator());
    body.push_back(tab_bar->Render() | center);
    body.push_back(separator());
    body.push_back(RenderPage() | flex);
    body.push_back(separator());
    body.push_back(hbox(std::move(footer_controls)) | center);
    body.push_back(text(message_.empty() ? " " : message_) | dim);
    if (refreshing_) body.push_back(text("Refreshing cluster data...") | dim);
    body.push_back(text("Touch the controls or use Tab, arrows, Enter; q/Esc quits.") |
                    dim);
    return vbox(std::move(body)) | border;
  });

  root |= CatchEvent([this](Event event) {
    if (event == Event::Character('q') || event == Event::Escape) {
      if (!pending_action_node_name_.empty()) {
        pending_action_node_name_.clear();
        pending_action_.clear();
      } else {
        app_.Exit();
      }
      return true;
    }
    if (event == Event::Character('r')) {
      StartRefresh();
      return true;
    }
    if (event == Event::ArrowLeft) {
      if (selected_tab_ > 0) SelectTab(selected_tab_ - 1);
      return true;
    }
    if (event == Event::ArrowRight) {
      if (selected_tab_ + 1 < static_cast<int>(kTabs.size())) {
        SelectTab(selected_tab_ + 1);
      }
      return true;
    }
    if (event == Event::ArrowUp) {
      SelectNode(selected_node_ - 1);
      return true;
    }
    if (event == Event::ArrowDown) {
      SelectNode(selected_node_ + 1);
      return true;
    }
    if (event == Event::Return && !pending_action_node_name_.empty()) {
      ConfirmAction();
      return true;
    }
    return false;
  });

  return root;
}

ftxui::Element Dashboard::RenderPage() {
  using namespace ftxui;
  if (!loaded_) return text("Connecting to Kubernetes...");
  if (nodes_.empty()) {
    return vbox({text("No nodes found."), text(message_)});
  }
  switch (selected_tab_) {
    case 0:
    case 1:
    case 2:
      return RenderNodeCards();
    case 3:
      return RenderActionPage();
    case 4:
      return RenderSshPage();
  }
  return text("Unknown dashboard tab.");
}

ftxui::Element Dashboard::RenderNodeCards() {
  using namespace ftxui;
  FlexboxConfig config;
  config.Set(FlexboxConfig::Direction::Row)
      .Set(FlexboxConfig::Wrap::Wrap)
      .Set(FlexboxConfig::AlignItems::Stretch)
      .SetGap(1, 1);
  Elements cards;
  cards.reserve(nodes_.size());
  for (const ::Node& node : nodes_) {
    switch (selected_tab_) {
      case 0:
        cards.push_back(RenderResourceNode(node));
        break;
      case 1:
        cards.push_back(RenderNetworkNode(node));
        break;
      case 2:
        cards.push_back(RenderStorageNode(node));
        break;
    }
  }
  return flexbox(std::move(cards), config) | frame | vscroll_indicator;
}

ftxui::Element Dashboard::RenderResourceNode(const ::Node& node) const {
  using namespace ftxui;
  return NodeCard(node, {
      Metric("CPU utilization", FormatPercent(node.stats.cpu_percent)),
      Metric("Memory utilization", FormatPercent(node.stats.memory_percent)),
      Metric("Uptime", FormatUptime(node.stats.uptime_seconds)),
      Metric("Temperature", FormatTemperature(node.stats.temperature_celsius)),
  });
}

ftxui::Element Dashboard::RenderNetworkNode(const ::Node& node) const {
  using namespace ftxui;
  const std::string ip = node.stats.internal_ip.empty()
                             ? (node.internal_ip.empty() ? "N/A" : node.internal_ip)
                             : node.stats.internal_ip;
  const std::string interface =
      node.stats.interface_name.empty() ? "N/A" : node.stats.interface_name;
  return NodeCard(node, {
      Metric("Node IP", ip),
      Metric("Interface", interface),
      Metric("Link speed", node.stats.link_speed.empty() ? "N/A"
                                                          : node.stats.link_speed),
      Metric("Download", FormatRate(node.stats.receive_bytes_per_second)),
      Metric("Upload", FormatRate(node.stats.transmit_bytes_per_second)),
      Metric("WAN ping (1.1.1.1)", node.stats.ping_ms < 0
                                       ? "N/A"
                                       : std::to_string(node.stats.ping_ms) + " ms"),
  });
}

ftxui::Element Dashboard::RenderStorageNode(const ::Node& node) const {
  using namespace ftxui;
  return NodeCard(node, {
      Metric("Root filesystem used", FormatPercent(node.stats.disk_used_percent)),
      Metric("Available", node.stats.disk_available.empty()
                              ? "N/A"
                              : node.stats.disk_available + " KB"),
      Metric("Mount", "/"),
  });
}

ftxui::Element Dashboard::RenderActionPage() {
  using namespace ftxui;
  Elements cards;
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    Element card = vbox({
                     node_buttons_[i]->Render() | bold | color(Color::Cyan),
                     text(nodes_[i].ready == "True" ? "READY" : "NOT READY") |
                         dim,
                     text(nodes_[i].internal_ip.empty() ? "IP unavailable"
                                                        : nodes_[i].internal_ip) |
                         dim,
                   }) |
                   border;
    if (static_cast<int>(i) == selected_node_) {
      card |= color(Color::Cyan) | bold;
    }
    cards.push_back(std::move(card));
  }
  FlexboxConfig config;
  config.Set(FlexboxConfig::Direction::Row)
      .Set(FlexboxConfig::Wrap::Wrap)
      .SetGap(1, 1);

  Elements content{
      text("Select a node, then choose an action. Shutdown and reboot require "
           "SSH key authentication and passwordless sudo.") |
          dim,
      flexbox(std::move(cards), config) | frame | vscroll_indicator,
      text("Selected: " + nodes_[selected_node_].name) | bold,
  };
  if (!pending_action_node_name_.empty()) {
    content.push_back(
        text("Confirm " + pending_action_ + " for " +
             pending_action_node_name_ + "?") |
        color(Color::Red) | bold);
  }
  return vbox(std::move(content));
}

ftxui::Element Dashboard::RenderSshPage() {
  using namespace ftxui;
  const ::Node& node = nodes_[selected_node_];
  FlexboxConfig config;
  config.Set(FlexboxConfig::Direction::Row)
      .Set(FlexboxConfig::Wrap::Wrap)
      .SetGap(1, 1);
  Elements selectors;
  for (std::size_t i = 0; i < node_buttons_.size(); ++i) {
    Element selector = node_buttons_[i]->Render();
    if (static_cast<int>(i) == selected_node_) {
      selector |= bold | color(Color::Cyan);
    }
    selectors.push_back(std::move(selector) | border);
  }
  return vbox({
      text("SSH terminal") | bold | color(Color::Cyan),
      separator(),
      flexbox(std::move(selectors), config) | frame | vscroll_indicator,
      text("Selected node: " + node.name),
      text("Target: " + client_.SshTarget(node)),
      text("Connect uses the system ssh client and your SSH config/keys."),
      text("Press Open SSH terminal to leave the dashboard temporarily; "
           "the dashboard returns when SSH exits.") |
          dim,
  }) |
         border | flex;
}
