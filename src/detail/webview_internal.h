#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <variant>

#include <huxerui/webview.h>

#define WEBVIEW_UNUSED(name) static_cast<void>(name)

namespace huxerui::detail {

inline constexpr char web_view_type[] = "huxerui/webview";

enum class WebViewCommand {
  Reload,
  StopLoading,
  GoBack,
  GoForward,
};

struct WebViewLoadRequestCommand {
  WebViewRequest request;
};

struct WebViewLoadHtmlCommand {
  WebViewHtml html;
};

struct WebViewEvaluateJavaScriptCommand {
  std::string script;
  WebViewJavaScriptCompletion completion;
};

extern const char web_view_page_metadata_script[];

WebViewPageMetadata DecodePageMetadata(std::int64_t document_id, std::string_view result);

template <class Instance>
std::int64_t BeginPageMetadata(Instance& instance) {
  const auto document_id = ++instance.metadata_document_id;
  instance.events.template Emit<WebViewEvents::PageMetadataChanged>(WebViewPageMetadata{.document_id = document_id});
  return document_id;
}

template <class Instance>
WebViewEvaluateJavaScriptCommand ReadPageMetadata(const std::shared_ptr<Instance>& instance, std::int64_t document_id) {
  return {
      .script = web_view_page_metadata_script,
      .completion = [weak = std::weak_ptr(instance), document_id](PlatformResult<std::string> result) {
        const auto current = weak.lock();
        const auto* value = std::get_if<std::string>(&result);
        if (!current || current->disposed || document_id == 0 || current->metadata_document_id != document_id || !value) {
          return;
        }
        auto metadata = DecodePageMetadata(document_id, *value);
        if (!metadata.icon_urls.empty() || metadata.description || metadata.theme_color) {
          current->events.template Emit<WebViewEvents::PageMetadataChanged>(std::move(metadata));
        }
      },
  };
}

struct WebViewGetCookiesCommand {
  std::string url;
  WebViewJavaScriptCompletion completion;
};

struct WebViewClearCookiesCommand {
  WebViewJavaScriptCompletion completion;
};

struct WebViewSetCookieCommand {
  std::string url;
  WebViewCookie cookie;
  WebViewJavaScriptCompletion completion;
};

struct WebViewDeleteCookieCommand {
  std::string url;
  std::string name;
  std::string domain;
  std::string path;
  WebViewJavaScriptCompletion completion;
};

struct WebViewGetCookieListCommand {
  std::string url;
  WebViewCookieListCompletion completion;
};

using WebViewControllerCommand = std::variant<
    WebViewCommand,
    WebViewLoadRequestCommand,
    WebViewLoadHtmlCommand,
    WebViewEvaluateJavaScriptCommand,
    WebViewGetCookiesCommand,
    WebViewClearCookiesCommand,
    WebViewSetCookieCommand,
    WebViewDeleteCookieCommand,
    WebViewGetCookieListCommand
>;

using WebViewCommandHandler = std::function<bool(WebViewControllerCommand)>;

struct WebViewControllerAccess {
  static void Connect(const WebViewController& controller, WebViewCommandHandler handler);
  static void Disconnect(const WebViewController& controller);
};

void InstallPlatformWebView(ApplicationContext& root);

} // namespace huxerui::detail
