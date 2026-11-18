#include <stdexcept>

#include "detail/webview_internal.h"

namespace huxerui::detail {

void InstallPlatformWebView(ApplicationContext&) {
  throw std::logic_error(
      "HuxerUI WebView requires a HuxerUI SDK build that provides the GTK4 PlatformView host "
      "(huxerui/linux/platform_registry.h)"
  );
}

} // namespace huxerui::detail
