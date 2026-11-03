#pragma once

#include <juce_core/juce_core.h>

namespace dab
{
// Thread-safe diagnostic log: %APPDATA%\DesktopAudioBridge\log.txt
// Never call from the audio thread (file I/O).
inline void log(const juce::String& message)
{
    static juce::FileLogger* logger = []
    {
        auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("DesktopAudioBridge").getChildFile("log.txt");
        f.getParentDirectory().createDirectory();
        if (f.getSize() > 2 * 1024 * 1024) f.deleteFile();
        return new juce::FileLogger(f, "---- session start ----", 0);
    }();

    logger->logMessage(juce::Time::getCurrentTime().formatted("%H:%M:%S") + "."
                       + juce::String(juce::Time::getMillisecondCounter() % 1000).paddedLeft('0', 3)
                       + " [t" + juce::String::toHexString((juce::pointer_sized_int) juce::Thread::getCurrentThreadId() & 0xffff)
                       + "] " + message);
}
} // namespace dab
