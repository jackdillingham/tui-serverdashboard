#pragma once

#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "kubernetes_client.hpp"
#include "node.hpp"

class Dashboard {
 public:
  explicit Dashboard(ftxui::App& app);
  ~Dashboard();
  ftxui::Component Build();
  void Activate();

 private:
  void StartRefresh();
  void RebuildNodeButtons();
  void SelectTab(int tab);
  void SelectNode(int index);
  void RequestAction(const std::string& action);
  void ConfirmAction();
  void ConnectSsh();
  ftxui::Element RenderPage();
  ftxui::Element RenderNodeCards();
  ftxui::Element RenderResourceNode(const Node& node) const;
  ftxui::Element RenderNetworkNode(const Node& node) const;
  ftxui::Element RenderStorageNode(const Node& node) const;
  ftxui::Element RenderActionPage();
  ftxui::Element RenderSshPage();

  ftxui::App& app_;
  KubernetesClient client_;
  std::vector<Node> nodes_;
  std::vector<ftxui::Component> tab_buttons_;
  std::vector<ftxui::Component> node_buttons_;
  ftxui::Component node_selection_container_;
  ftxui::Component refresh_button_;
  ftxui::Component previous_node_button_;
  ftxui::Component next_node_button_;
  ftxui::Component confirm_button_;
  ftxui::Component cancel_button_;
  ftxui::Component shutdown_button_;
  ftxui::Component reboot_button_;
  ftxui::Component wake_button_;
  ftxui::Component ssh_button_;
  int selected_tab_ = 0;
  int selected_node_ = 0;
  std::string pending_action_node_name_;
  std::string pending_action_;
  std::string message_;
  bool loaded_ = false;
  bool refreshing_ = false;
  std::thread refresh_worker_;
};
