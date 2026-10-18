#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

void resources();
void network();
void storage();
void actions();
void ssh();

int main() {
  using namespace ftxui;

  int currentTab = 0;
  bool started = false;

  auto screen = App::FitComponent();


  // START MENU

  auto start = Renderer([&] {
    return vbox({
               text("ServerDash") | bold | center,
               separator(),
               text(started ? "Main menu goes here." : "FTXUI is ready."),
               text(""),
               text(started ? "" : "Press enter to continue.") | dim,
               text("Press q or Escape to quit.") | dim,
           }) |
           border | center;
  });




  // MAIN MENU
  auto mainMenu = Renderer([&] {
    return vbox({
               text("Main Menu") | bold | center,
               separator(),
               text("1. Resources"),
               text("2. Network"),
               text("3. Storage"),
               text("4. Actions"),
               text("5. SSH"),
               text("Press q or Escape to quit.") | dim,
           }) |
           border | center;
  });



  start |= CatchEvent([&](Event event) {
    if (event == Event::Return) {
      started = true;
      screen.Loop(mainMenu);
    }

    if (event == Event::Character('q') || event == Event::Escape) {
      screen.Exit();
      return true;
    }

    return false;
  });



  if (started == false) {
    screen.Loop(start);
  }
  if (started == true) {
    // Main menu goes here
  }
  return 0;
}