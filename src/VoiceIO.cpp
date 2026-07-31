#include "VoiceIO.h"
#include <sapi.h>
#include <sphelper.h>
#include <algorithm>

VoiceIO::VoiceIO(const Config& cfg) : m_cfg(cfg) {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // ── Text-to-speech ──────────────────────────────────────────────────────
    if (cfg.enableOutput) {
        ISpVoice* voice = nullptr;
        HRESULT hr = ::CoCreateInstance(
            __uuidof(SpVoice), nullptr, CLSCTX_ALL,
            __uuidof(ISpVoice), reinterpret_cast<void**>(&voice)
        );
        if (SUCCEEDED(hr)) {
            m_voice = voice;
            WINBOT_INFO("VoiceIO: TTS ready");
        } else {
            WINBOT_WARN("VoiceIO: TTS init failed 0x{:08X}", static_cast<uint32_t>(hr));
        }
    }

    // ── Speech recognition ──────────────────────────────────────────────────
    if (cfg.enableInput) {
        ISpRecognizer* recognizer = nullptr;
        HRESULT hr = ::CoCreateInstance(
            __uuidof(SpInprocRecognizer), nullptr, CLSCTX_ALL,
            __uuidof(ISpRecognizer), reinterpret_cast<void**>(&recognizer)
        );
        if (SUCCEEDED(hr)) {
            m_recognizer = recognizer;

            // Set up the audio input to the default microphone
            recognizer->SetInput(nullptr, TRUE); // nullptr = use default audio input

            // Create recognition context
            ISpRecoContext* context = nullptr;
            recognizer->CreateRecoContext(&context);
            m_context = context;

            // Create a dictation grammar (accepts any speech)
            ISpRecoGrammar* grammar = nullptr;
            if (context) {
                context->CreateGrammar(1, &grammar);
                grammar->LoadDictation(nullptr, SPLO_STATIC);
                grammar->SetDictationState(SPRS_ACTIVE);
                m_grammar = grammar;
            }
            WINBOT_INFO("VoiceIO: speech recognition ready (wake word: '{}')", cfg.wakeWord);
        } else {
            WINBOT_WARN("VoiceIO: speech recognition init failed 0x{:08X}", static_cast<uint32_t>(hr));
        }
    }
}

VoiceIO::~VoiceIO() {
    if (m_grammar)    { reinterpret_cast<ISpRecoGrammar*>(m_grammar)->Release();    }
    if (m_context)    { reinterpret_cast<ISpRecoContext*>(m_context)->Release();    }
    if (m_recognizer) { reinterpret_cast<ISpRecognizer*>(m_recognizer)->Release();  }
    if (m_voice)      { reinterpret_cast<ISpVoice*>(m_voice)->Release();            }
    ::CoUninitialize();
}

void VoiceIO::speak(std::string_view text) {
    if (!m_voice || !m_cfg.enableOutput) return;
    auto* voice = reinterpret_cast<ISpVoice*>(m_voice);
    std::wstring wtext = utf8_to_wide(text);
    // SPF_ASYNC = non-blocking
    voice->Speak(wtext.c_str(), SPF_ASYNC, nullptr);
}

std::string VoiceIO::listenForCommand() {
    if (!m_context || !m_cfg.enableInput) return {};

    auto* context = reinterpret_cast<ISpRecoContext*>(m_context);
    std::string wakeWordLower(m_cfg.wakeWord);
    to_lower_inplace(wakeWordLower);

    WINBOT_INFO("VoiceIO: listening for wake word '{}'...", m_cfg.wakeWord);

    while (true) {
        SPEVENT event{};
        ULONG fetched = 0;
        context->GetEvents(1, &event, &fetched);

        if (fetched > 0 && event.eEventId == SPEI_RECOGNITION) {
            auto* result = reinterpret_cast<ISpRecoResult*>(event.lParam);
            SPPHRASE* phrase = nullptr;
            if (SUCCEEDED(result->GetPhrase(&phrase)) && phrase) {
                // Get recognized text
                WCHAR* text = nullptr;
                result->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &text, nullptr);
                std::string recognized;
                if (text) {
                    recognized = wide_to_utf8(text);
                    ::CoTaskMemFree(text);
                }
                ::CoTaskMemFree(phrase);

                std::string lower(recognized);
                to_lower_inplace(lower);

                // Check for wake word
                if (lower.contains(wakeWordLower)) {
                    // Strip the wake word from the recognized text
                    auto pos = lower.find(wakeWordLower);
                    std::string command = recognized.substr(
                        pos + m_cfg.wakeWord.size()
                    );
                    // Trim leading whitespace
                    while (!command.empty() && std::isspace(static_cast<unsigned char>(command[0])))
                        command.erase(command.begin());
                    if (!command.empty()) return command;
                }
            }
        }
        ::Sleep(50);
    }
}
