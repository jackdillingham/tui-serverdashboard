#include <string>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "dashboard.hpp"

int main() {
  using namespace ftxui;

  App app = App::Fullscreen();
  int active_page = 0;
  auto dashboard = std::make_shared<Dashboard>(app);
  Component dashboard_component = dashboard->Build();

  auto enter_dashboard = [&] {
    active_page = 1;
    dashboard->Activate();
  };
  auto start_button = Button("Open dashboard", enter_dashboard);
  auto start = Renderer(start_button, [&] {
    return vbox({
               text("ServerDash") | bold | center,
               separator(),
               text("Kubernetes node dashboard") | center,
               text(""),
               start_button->Render() | center,
               text(""),
               text("Enter to continue  |  q or Escape to quit") | dim | center,
           }) |
           border | center;
  });
  start |= CatchEvent([&](Event event) {
    if (event == Event::Return) {
      enter_dashboard();
      return true;
    }
    if (event == Event::Character('q') || event == Event::Escape) {
      app.Exit();
      return true;
    }
    return false;
  });

  auto main_component =
      Container::Tab({start, dashboard_component}, &active_page);
  app.Loop(main_component);
  return 0;
}
