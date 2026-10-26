#include "ui/dialog.h"

#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

namespace ptoolkit
{
Dialog::Dialog(std::function<void()> on_close) : m_on_close(std::move(on_close))
{
  auto options = ftxui::InputOption::Default();
  options.multiline = false;
  options.transform = [](ftxui::InputState state) { return state.element; };
  m_input = ftxui::Input(&m_value, "Preset name", options);
  Add(m_input);
}
void Dialog::Open(Kind kind, std::string title, std::string message,
                  Confirm confirm)
{
  m_kind = kind;
  m_title = std::move(title);
  m_message = std::move(message);
  m_confirm = std::move(confirm);
  m_visible = true;
  TakeFocus();
}
void Dialog::Message(std::string title, std::string message)
{
  Open(Kind::Message, std::move(title), std::move(message), {});
}
void Dialog::Prompt(std::string title, std::string label, std::string value,
                    Confirm confirm)
{
  m_value = std::move(value);
  Open(Kind::Prompt, std::move(title), std::move(label), std::move(confirm));
  m_input->OnEvent(ftxui::Event::End);
}
void Dialog::ConfirmAction(std::string title, std::string message,
                           Confirm confirm)
{
  Open(Kind::Confirm, std::move(title), std::move(message), std::move(confirm));
}
ftxui::Component Dialog::ActiveChild()
{
  return m_kind == Kind::Prompt ? m_input : nullptr;
}
void Dialog::Close(bool confirm)
{
  m_visible = false;
  auto callback = std::move(m_confirm);
  const auto value = m_value;
  m_on_close();
  if (confirm && callback) callback(value);
}
bool Dialog::OnEvent(ftxui::Event event)
{
  if (!m_visible) return false;
  if (event == ftxui::Event::Return)
    Close(true);
  else if (event == ftxui::Event::Escape && m_kind != Kind::Message)
    Close(false);
  else if (m_kind == Kind::Prompt)
    m_input->OnEvent(event);
  return true;
}
ftxui::Element Dialog::OnRender()
{
  using namespace ftxui;
  Elements contents{text(m_title) | bold, separator(), paragraph(m_message)};
  if (m_kind == Kind::Prompt) contents.push_back(m_input->Render());
  contents.push_back(text(m_kind == Kind::Message
                              ? "Enter: close"
                              : "Enter: confirm    Esc: cancel") |
                     dim);
  return vbox(std::move(contents)) | size(WIDTH, EQUAL, 52) | borderEmpty |
         borderStyled(ROUNDED, Color::White) |
         color(Color::RGB(0xf1, 0xf5, 0xfb)) |
         bgcolor(Color::RGB(0x54, 0x84, 0xb9)) | clear_under | center;
}
}  // namespace ptoolkit
