#include "mix-desktop/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace mastering::desktop {

class MasteringAudioApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Mastering Audio Suite"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow_ != nullptr)
            mainWindow_->toFront(true);
    }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(
                std::move(name),
                juce::Colour(0xff090b10),
                DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            centreWithSize(1'360, 840);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace mastering::desktop

START_JUCE_APPLICATION(mastering::desktop::MasteringAudioApplication)
