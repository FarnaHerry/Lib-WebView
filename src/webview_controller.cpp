#include <huxerui/webview.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <utility>

#include "detail/webview_internal.h"

namespace huxerui::detail {

class WebViewControllerState {
public:
  WebViewCommandHandler handler;

  // Load commands issued before a WebView connects are retained here and
  // replayed on connect: loading is final-state semantics (the last load
  // wins and replays idempotently), so buffering bridges the gap between
  // composition-time callers and the platform view that mounts one frame
  // later. Ephemeral commands (reload/stop/history) and callback-bearing
  // commands (JavaScript, cookies) are not buffered.
  std::optional<WebViewControllerCommand> pending_load;
};

void WebViewControllerAccess::Connect(
    const WebViewController& controller,
    WebViewCommandHandler handler
) {
  controller.state_->handler = std::move(handler);
  if (controller.state_->pending_load) {
    auto replay = std::move(*controller.state_->pending_load);
    controller.state_->pending_load.reset();
    std::invoke(controller.state_->handler, std::move(replay));
  }
}

void WebViewControllerAccess::Disconnect(const WebViewController& controller) {
  controller.state_->handler = {};
}

} // namespace huxerui::detail

namespace huxerui {

WebViewController::WebViewController()
    : state_(std::make_shared<detail::WebViewControllerState>()) {}

bool WebViewController::IsConnected() const noexcept {
  return state_ && static_cast<bool>(state_->handler);
}

namespace {

void BufferLoad(
    const std::shared_ptr<detail::WebViewControllerState>& state,
    detail::WebViewControllerCommand command
) {
  if (state && !state->handler) {
    state->pending_load = std::move(command);
  }
}

std::string UppercaseAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

bool RunCommand(
    const std::shared_ptr<detail::WebViewControllerState>& state,
    detail::WebViewControllerCommand command
) {
  return state && state->handler && state->handler(std::move(command));
}

} // namespace

bool WebViewController::Reload() const {
  return RunCommand(state_, detail::WebViewCommand::Reload);
}

bool WebViewController::StopLoading() const {
  return RunCommand(state_, detail::WebViewCommand::StopLoading);
}

bool WebViewController::GoBack() const {
  return RunCommand(state_, detail::WebViewCommand::GoBack);
}

bool WebViewController::GoForward() const {
  return RunCommand(state_, detail::WebViewCommand::GoForward);
}

bool WebViewController::LoadUrl(std::string url) const {
  return LoadRequest({.url = std::move(url)});
}

bool WebViewController::LoadRequest(WebViewRequest request) const {
  request.method = UppercaseAscii(std::move(request.method));
  detail::WebViewLoadRequestCommand command{std::move(request)};
  if (!RunCommand(state_, command)) {
    BufferLoad(state_, detail::WebViewControllerCommand{std::move(command)});
    return false;
  }
  return true;
}

bool WebViewController::LoadHtml(WebViewHtml html) const {
  detail::WebViewLoadHtmlCommand command{std::move(html)};
  if (!RunCommand(state_, command)) {
    BufferLoad(state_, detail::WebViewControllerCommand{std::move(command)});
    return false;
  }
  return true;
}

bool WebViewController::EvaluateJavaScript(
    std::string script,
    WebViewJavaScriptCompletion completion
) const {
  if (!completion) {
    return false;
  }
  return RunCommand(
      state_,
      detail::WebViewEvaluateJavaScriptCommand{
          .script = std::move(script),
          .completion = std::move(completion),
      }
  );
}

bool WebViewController::GetCookies(std::string url, WebViewJavaScriptCompletion completion) const {
  if (!completion || url.empty()) return false;
  return RunCommand(state_, detail::WebViewCookieCommand{
      .url = std::move(url), .completion = std::move(completion)});
}

bool WebViewController::ClearCookies(WebViewJavaScriptCompletion completion) const {
  if (!completion) return false;
  return RunCommand(state_, detail::WebViewCookieCommand{
      .clear = true, .completion = std::move(completion)});
}

} // namespace huxerui
