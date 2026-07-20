#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <optional>

namespace mastering::app {

class WebViewComponent final : public juce::Component {
public:
    enum class Product {
        analyzerPlugin,
        desktopSuite,
        mixNode
    };

    explicit WebViewComponent(Product product);
    ~WebViewComponent() override;

    void resized() override;
    void setCommandHandler(std::function<void(const juce::var&)> handler);
    void pushState(const juce::var& state);

private:
    [[nodiscard]] std::optional<juce::WebBrowserComponent::Resource> resourceForUrl(
        const juce::String& url) const;

    Product product_;
    std::function<void(const juce::var&)> commandHandler_;
    std::unique_ptr<juce::WebBrowserComponent> browser_;
};

} // namespace mastering::app
