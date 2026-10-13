#pragma once
#include "profile_catalogue.h"
#include "ui_theme.h"
#include <windows.h>
#include <functional>
#include <array>

namespace gbrowser::webview {
// Owned native modeless window with a disabled owner: the normal STA pump keeps
// WebView2 callbacks alive. It never reads browser databases or accepts paths.
class ProfileDialog {
 public:
  ProfileDialog(ProfileCatalogue& catalogue, std::string active, UiTheme& theme, std::function<void(std::string)> select);
  ~ProfileDialog();
  void Open(HWND owner);
  void Close();
  bool Translate(MSG& message) const;
 private:
  static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  void CreateControls();
  void Layout();
  void Refresh(const std::string& selected = {});
  void Action(int action);
  std::string Selection() const;
  ProfileCatalogue& catalogue_;
  UiTheme& theme_;
  std::string active_;
  std::function<void(std::string)> select_;
  HWND window_ = nullptr, owner_ = nullptr, list_ = nullptr, name_ = nullptr, hint_ = nullptr;
  HFONT font_ = nullptr;
  std::array<HWND, 7> buttons_{};
  std::vector<std::string> rows_;
  bool recycled_ = false;
};
}
