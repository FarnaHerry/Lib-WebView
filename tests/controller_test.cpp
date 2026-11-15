#include <huxerui/webview.h>
#include "detail/webview_internal.h"
#include <cstdio>
#include <variant>

int main() {
  using namespace huxerui;
  WebViewController controller;
  int failures = 0, queries = 0, clears = 0;
  auto check = [&](bool passed, const char* message) {
    if (!passed) { std::printf("FAIL %s\n", message); ++failures; }
  };
  {
    using detail::BeginPageMetadata;
    using detail::ReadPageMetadata;
    struct MetadataView {
      PlatformEventEmitter events;
      std::int64_t metadata_document_id = 0;
      bool disposed = false;
    };
    const auto make_view = [&](std::vector<WebViewPageMetadata>& snapshots) {
      return std::make_shared<MetadataView>(MetadataView{
          .events = detail::MakePlatformEventEmitter(
              [&snapshots, &check](std::type_index event, PlatformValue payload) -> std::optional<PlatformValue> {
                check(event == typeid(WebViewEvents::PageMetadataChanged), "metadata uses the typed WebView event");
                snapshots.push_back(payload.Get<WebViewPageMetadata>());
                return std::nullopt;
              },
              {}),
      });
    };
    std::vector<WebViewPageMetadata> first_snapshots, second_snapshots;
    const auto first = make_view(first_snapshots);
    const auto second = make_view(second_snapshots);
    const auto old_document = BeginPageMetadata(*first);
    const auto old_read = ReadPageMetadata(first, old_document);
    const auto other_document = BeginPageMetadata(*second);
    const auto other_read = ReadPageMetadata(second, other_document);
    const auto new_document = BeginPageMetadata(*first);
    const auto new_read = ReadPageMetadata(first, new_document);
    old_read.completion(std::string(R"("old||https%3A%2F%2Fexample.com%2Ficon-old.png")"));
    check(first_snapshots == std::vector<WebViewPageMetadata>{{.document_id = old_document},
                                                            {.document_id = new_document}},
          "new navigation clears metadata and rejects the previous document result");
    other_read.completion(std::string(R"("other||https%3A%2F%2Fexample.com%2Ficon-other.png")"));
    check(second_snapshots.size() == 2 && second_snapshots.back().description == "other" && first_snapshots.size() == 2,
          "another mounted WebView publishes independently even when document IDs match");

    const std::string result =
        R"("%E7%BD%91%E9%A1%B5%20%7C%20%22hello%22|%23abc|)"
        R"(https%3A%2F%2Fexample.com%2Ficon.png|https%3A%2F%2Fexample.com%2Ficon.svg")";
    const WebViewPageMetadata expected{
        .document_id = new_document,
        .icon_urls = {"https://example.com/icon.png", "https://example.com/icon.svg"},
        .description = "\xe7\xbd\x91\xe9\xa1\xb5 | \"hello\"",
        .theme_color = "#abc",
    };
    new_read.completion(result);
    check(first_snapshots.size() == 3 && first_snapshots.back() == expected, "metadata fields decode in document order");
    check(WebViewPageMetadata::Decode(PlatformPayload::Object{
              {"documentId", new_document}, {"result", result},
          }) == expected, "Android payload decodes to the same metadata snapshot");
    check(detail::DecodePageMetadata(new_document, R"("%FF|color|icon")") ==
              WebViewPageMetadata{.document_id = new_document},
          "malformed metadata is ignored");
    const auto disposed_read = ReadPageMetadata(first, BeginPageMetadata(*first));
    first->disposed = true;
    disposed_read.completion(result);
    check(first_snapshots.size() == 4, "disposed WebViews drop pending metadata updates");
  }
  {
    WebViewController buffered;
    int loads = 0;
    std::string last_url;
    check(!buffered.LoadUrl("https://example.com/one"), "disconnected load reports not accepted");
    check(!buffered.LoadUrl("https://example.com/two"), "second disconnected load reports not accepted");
    detail::WebViewControllerAccess::Connect(buffered, [&](detail::WebViewControllerCommand command) {
      if (auto* load = std::get_if<detail::WebViewLoadRequestCommand>(&command)) {
        ++loads;
        last_url = load->request.url;
        return true;
      }
      return false;
    });
    check(loads == 1 && last_url == "https://example.com/two", "last buffered load replays on connect");
    check(buffered.IsConnected(), "buffered controller is connected");
  }

  const PlatformPayload system_theme = WebViewProperties::Encode({});
  check(system_theme.AsObject().at("themeBrightness").IsNull(), "system theme encodes as null");
  check(!system_theme.AsObject().at("domStorageEnabled").AsBoolean(), "DOM storage is off by default");
  check(system_theme.AsObject().at("backgroundColor").AsObject().at("alpha").AsDouble() == 0.0,
        "WebView surface is transparent by default");
  const PlatformPayload light_theme = WebViewProperties::Encode({
      .theme_brightness = ThemeBrightness::Light,
  });
  check(light_theme.AsObject().at("themeBrightness").AsString() == "light", "light theme encoding");
  const PlatformPayload dark_theme = WebViewProperties::Encode({
      .user_agent = "Test WebView",
      .theme_brightness = ThemeBrightness::Dark,
      .dom_storage_enabled = true,
      .background_color = Color{.red = 1.0F, .green = 0.5F, .blue = 0.25F, .alpha = 0.75F},
  });
  check(dark_theme.AsObject().at("themeBrightness").AsString() == "dark", "dark theme encoding");
  check(dark_theme.AsObject().at("userAgent").AsString() == "Test WebView", "custom user agent encoding");
  check(dark_theme.AsObject().at("domStorageEnabled").AsBoolean(), "DOM storage opt-in encoding");
  const auto& background = dark_theme.AsObject().at("backgroundColor").AsObject();
  check(background.at("red").AsDouble() == 1.0 && background.at("green").AsDouble() == 0.5 &&
            background.at("blue").AsDouble() == 0.25 && background.at("alpha").AsDouble() == 0.75,
        "surface color channel encoding");
  const WebViewCookie cookie{
      .name = "session",
      .value = "secret",
      .domain = "example.com",
      .path = "/api",
      .expires = std::chrono::system_clock::time_point(std::chrono::seconds(1700000000)),
      .secure = true,
      .http_only = true,
      .same_site = WebViewCookieSameSite::Strict,
  };
  check(WebViewCookie::Decode(WebViewCookie::Encode(cookie)) == cookie, "structured cookie round trip");
  bool completed = false;
  auto completion = [&](PlatformResult<std::string> result) {
    completed = std::holds_alternative<std::string>(result) &&
        std::get<std::string>(result) == "session=secret";
  };
  auto empty_completion = [&](PlatformResult<std::string> result) {
    completed = std::holds_alternative<std::string>(result) && std::get<std::string>(result).empty();
  };
  bool listed = false;
  auto list_completion = [&](PlatformResult<std::vector<WebViewCookie>> result) {
    listed = std::holds_alternative<std::vector<WebViewCookie>>(result) &&
        std::get<std::vector<WebViewCookie>>(result) == std::vector<WebViewCookie>{cookie};
  };
  check(!controller.GetCookies("https://example.com/api", completion), "disconnected query rejected");
  check(!controller.SetCookie("https://example.com/api", cookie, empty_completion), "disconnected set rejected");
  check(!controller.DeleteCookie("https://example.com/api", "session", "example.com", "/api", empty_completion),
        "disconnected delete rejected");
  check(!controller.GetCookieList("https://example.com/api", list_completion), "disconnected list rejected");
  check(!controller.ClearCookies(empty_completion), "disconnected clear rejected");
  int writes = 0, deletions = 0, lists = 0;
  detail::WebViewControllerAccess::Connect(controller, [&](detail::WebViewControllerCommand command) {
    if (auto* set = std::get_if<detail::WebViewSetCookieCommand>(&command)) {
      ++writes;
      check(set->url == "https://example.com/api" && set->cookie == cookie, "set command retains cookie");
      set->completion(std::string{});
      return true;
    }
    if (auto* deletion = std::get_if<detail::WebViewDeleteCookieCommand>(&command)) {
      ++deletions;
      check(deletion->url == "https://example.com/api" && deletion->name == "session" &&
            deletion->domain == "example.com" && deletion->path == "/api", "delete command retains identity");
      deletion->completion(std::string{});
      return true;
    }
    if (auto* list = std::get_if<detail::WebViewGetCookieListCommand>(&command)) {
      ++lists;
      check(list->url == "https://example.com/api", "list command retains URL");
      list->completion(std::vector<WebViewCookie>{cookie});
      return true;
    }
    if (auto* cookies = std::get_if<detail::WebViewGetCookiesCommand>(&command)) {
      ++queries;
      check(cookies->url == "https://example.com/api", "cookie URL retained");
      cookies->completion(std::string("session=secret"));
      return true;
    }
    auto* clear = std::get_if<detail::WebViewClearCookiesCommand>(&command);
    check(clear != nullptr, "typed clear cookies command");
    if (!clear) return false;
    ++clears;
    clear->completion(std::string{});
    return true;
  });
  check(!controller.GetCookies("", completion), "empty URL rejected");
  check(!controller.GetCookies("https://example.com/api", {}), "missing completion rejected");
  check(!controller.ClearCookies({}), "missing clear completion rejected");
  check(!controller.SetCookie("", cookie, empty_completion), "set requires URL");
  check(!controller.SetCookie("https://example.com/api", WebViewCookie{.domain = "example.com"}, empty_completion),
        "set requires name");
  check(!controller.SetCookie("https://example.com/api", WebViewCookie{.name = "session"}, empty_completion),
        "set requires domain");
  WebViewCookie insecure_none = cookie;
  insecure_none.same_site = WebViewCookieSameSite::None;
  insecure_none.secure = false;
  check(!controller.SetCookie("https://example.com/api", insecure_none, empty_completion),
        "SameSite None requires Secure");
  WebViewCookie invalid_path = cookie;
  invalid_path.path = "api";
  check(!controller.SetCookie("https://example.com/api", invalid_path, empty_completion), "set requires absolute path");
  WebViewCookie invalid_text = cookie;
  invalid_text.value = "secret; Secure";
  check(!controller.SetCookie("https://example.com/api", invalid_text, empty_completion), "set rejects header injection");
  check(!controller.SetCookie("https://example.com/api", cookie, {}), "set requires completion");
  check(!controller.DeleteCookie("", "session", "example.com", "/api", empty_completion), "delete requires URL");
  check(!controller.DeleteCookie("https://example.com/api", "", "example.com", "/api", empty_completion),
        "delete requires name");
  check(!controller.DeleteCookie("https://example.com/api", "session", "", "/api", empty_completion),
        "delete requires domain");
  check(!controller.DeleteCookie("https://example.com/api", "session", "example.com", "api", empty_completion),
        "delete requires absolute path");
  check(!controller.DeleteCookie("https://example.com/api", "session", "example.com", "/api", {}),
        "delete requires completion");
  check(!controller.GetCookieList("", list_completion), "list requires URL");
  check(!controller.GetCookieList("https://example.com/api", {}), "list requires completion");
  check(controller.GetCookies("https://example.com/api", completion) && completed && queries == 1 && clears == 0,
        "query returns result without clearing cookies");
  bool cleared = false;
  check(controller.ClearCookies([&](PlatformResult<std::string> result) {
    cleared = std::holds_alternative<std::string>(result) && std::get<std::string>(result).empty();
  }) && cleared && clears == 1 && queries == 1, "clear completes without querying cookies");
  completed = false;
  check(controller.SetCookie("https://example.com/api", cookie, empty_completion) && completed && writes == 1,
        "set completes with empty string");
  completed = false;
  check(controller.DeleteCookie("https://example.com/api", "session", "example.com", "/api", empty_completion) &&
        completed && deletions == 1, "delete completes with empty string");
  check(controller.GetCookieList("https://example.com/api", list_completion) && listed && lists == 1,
        "list returns structured cookies");
  detail::WebViewControllerAccess::Disconnect(controller);
  check(!controller.ClearCookies(completion) && clears == 1, "disconnect prevents clear");
  check(!controller.GetCookies("https://example.com/api", completion) && queries == 1,
        "disconnect prevents query");
  check(!controller.SetCookie("https://example.com/api", cookie, empty_completion) && writes == 1,
        "disconnect prevents set");
  check(!controller.DeleteCookie("https://example.com/api", "session", "example.com", "/api", empty_completion) &&
        deletions == 1, "disconnect prevents delete");
  check(!controller.GetCookieList("https://example.com/api", list_completion) && lists == 1,
        "disconnect prevents list");
  return failures ? 1 : 0;
}
