#pragma once
#include "character_shortcuts.h"
#include "ui_theme.h"
#include <functional>

namespace gbrowser::webview {
class ShortcutDialog {
 public:
  using Save = std::function<void(const characters::Bindings&, bool)>;
  ShortcutDialog(UiTheme& theme, Save save) : theme_(theme), save_(std::move(save)) {}
  ~ShortcutDialog() { Close(); if (font_) DeleteObject(font_); }
  void Open(HWND owner, characters::Bindings keys, bool enabled);
  void Close();
  bool Translate(MSG& message);
  HWND window() const { return window_; }
 private:
  static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
  void Layout();
  void Refresh();
  void Action(int id);
  void Record(unsigned key);
  UiTheme& theme_;
  Save save_;
  characters::Bindings keys_ = characters::Defaults();
  characters::KeyTracker tracker_;
  HWND window_ = nullptr, toggle_ = nullptr, hint_ = nullptr, reset_ = nullptr, save_button_ = nullptr, cancel_ = nullptr;
  std::array<HWND, characters::kCount> labels_{}, keys_buttons_{}, clear_{}, switches_{};
  std::array<HWND,4> groups_{};
  int group_ = 0;
  HFONT font_ = nullptr;
  int capture_ = -1;
  bool enabled_ = false;
};
}
