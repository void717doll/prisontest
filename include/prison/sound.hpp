#pragma once
#include <SDL_audio.h>
#include <array>
#include <filesystem>
#include <vector>

namespace prison {
enum class MiningSound { Dig, Break };

class SoundManager {
  public:
    explicit SoundManager(const std::filesystem::path& root);
    ~SoundManager();
    // the audio callback keeps this object's address until the device closes.
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    void play(MiningSound sound, float volume);

  private:
    struct Voice {
        const std::vector<Uint8>* sample = nullptr;
        std::size_t offset = 0;
        int volume = 0;
    };

    static void mix(void* userdata, Uint8* stream, int length);

    SDL_AudioDeviceID device_ = 0;
    std::array<std::vector<Uint8>, 6> sounds_;
    std::array<Voice, 8> voices_{};
    std::array<unsigned, 2> nextSample_{};
    std::array<Uint32, 2> lastPlay_{};
    SDL_AudioSpec spec_{};
    unsigned nextVoice_ = 0;
};
} // namespace prison
