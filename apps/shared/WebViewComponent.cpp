#include "shared/WebViewComponent.h"

#include <BinaryData.h>
#include <cstddef>

namespace mastering::app {
namespace {

juce::WebBrowserComponent::Resource binaryResource(
    const char* resourceName,
    const juce::String& mimeType)
{
    int size = 0;
    const auto* data = BinaryData::getNamedResource(resourceName, size);
    if (data == nullptr || size <= 0)
        return {};

    const auto* begin = reinterpret_cast<const std::byte*>(data);
    return {{begin, begin + size}, mimeType};
}

juce::String normalizedPath(juce::String url)
{
    const auto queryIndex = url.indexOfChar('?');
    if (queryIndex >= 0)
        url = url.substring(0, queryIndex);
    while (url.startsWithChar('/'))
        url = url.substring(1);
    return url;
}

} // namespace

WebViewComponent::WebViewComponent(Product product)
    : product_(product)
{
    auto options = juce::WebBrowserComponent::Options {}
        .withNativeIntegrationEnabled()
        .withEventListener(
            "masteringAudioCommand",
            [this](const juce::var& event) {
                if (commandHandler_)
                    commandHandler_(event);
            })
        .withResourceProvider([this](const auto& url) {
            return resourceForUrl(url);
        });

   #if JUCE_WINDOWS
    options = options
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(
            juce::WebBrowserComponent::Options::WinWebView2 {}
                .withUserDataFolder(
                    juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("MasteringAudioWebView")));
   #endif

    browser_ = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*browser_);

    auto entryUrl = juce::WebBrowserComponent::getResourceProviderRoot();
    if (product_ == Product::analyzerPlugin)
        entryUrl += "#plugin";
    browser_->goToURL(entryUrl);
}

WebViewComponent::~WebViewComponent() = default;

void WebViewComponent::resized()
{
    browser_->setBounds(getLocalBounds());
}

void WebViewComponent::setCommandHandler(
    std::function<void(const juce::var&)> handler)
{
    commandHandler_ = std::move(handler);
}

void WebViewComponent::pushState(const juce::var& state)
{
    if (browser_ != nullptr)
        browser_->emitEventIfBrowserIsVisible("masteringAudioState", state);
}

std::optional<juce::WebBrowserComponent::Resource> WebViewComponent::resourceForUrl(
    const juce::String& url) const
{
    const auto path = normalizedPath(url);
    if (path.isEmpty() || path == "index.html")
        return binaryResource("index_html", "text/html");
    if (path == "assets/app.js")
        return binaryResource("app_js", "text/javascript");
    if (path == "assets/app.css")
        return binaryResource("app_css", "text/css");
    return std::nullopt;
}

} // namespace mastering::app
