#ifndef WINBOT_VOICEIO_H
#define WINBOT_VOICEIO_H
#include "Common.h"

// ── VoiceIO ───────────────────────────────────────────────────────────────────
// Speech input/output using the Windows Speech API (SAPI).
// No external dependencies — uses the built-in Windows TTS and recognition.
class VoiceIO {
public:
    struct Config {
        bool   enableInput{ false };
        bool   enableOutput{ false };
        std::string wakeWord{ "hey winbot" };
    };

    explicit VoiceIO(const Config& cfg);
    ~VoiceIO();

    VoiceIO(const VoiceIO&)            = delete;
    VoiceIO& operator=(const VoiceIO&) = delete;

    // Speak text aloud (non-blocking — fires and forgets)
    void speak(std::string_view text);

    // Block until the wake word is detected, then return the full spoken command.
    // Returns empty string if input is disabled or recognition fails.
    [[nodiscard]] std::string listenForCommand();

    [[nodiscard]] bool inputEnabled()  const noexcept { return m_cfg.enableInput;  }
    [[nodiscard]] bool outputEnabled() const noexcept { return m_cfg.enableOutput; }

private:
    Config m_cfg;
    void*  m_voice{ nullptr };       // ISpVoice*
    void*  m_recognizer{ nullptr };  // ISpRecognizer*
    void*  m_context{ nullptr };     // ISpRecoContext*
    void*  m_grammar{ nullptr };     // ISpRecoGrammar*
};

#endif // WINBOT_VOICEIO_H
