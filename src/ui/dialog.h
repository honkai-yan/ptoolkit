#pragma once

#include <functional>
#include <string>

#include <ftxui/component/component_base.hpp>

namespace ptoolkit
{
// Modal contents only. The host overlays Render() and routes events exclusively
// to this component while Visible(); focus is restored before callbacks run.
class Dialog : public ftxui::ComponentBase
{
public:
  using Confirm = std::function<void(const std::string&)>;
  explicit Dialog(std::function<void()> on_close);
  void Message(std::string title, std::string message);
  void Prompt(std::string title, std::string label, std::string value,
              Confirm on_confirm);
  void ConfirmAction(std::string title, std::string message,
                     Confirm on_confirm);
  bool Visible() const { return m_visible; }
  bool Focusable() const override { return m_visible; }
  ftxui::Component ActiveChild() override;
  bool OnEvent(ftxui::Event event) override;
  ftxui::Element OnRender() override;

private:
  enum class Kind
  {
    Message,
    Prompt,
    Confirm
  };
  void Open(Kind kind, std::string title, std::string message, Confirm confirm);
  void Close(bool confirm);
  Kind m_kind = Kind::Message;
  bool m_visible = false;
  std::string m_title, m_message, m_value;
  ftxui::Component m_input;
  Confirm m_confirm;
  std::function<void()> m_on_close;
};
}  // namespace ptoolkit
