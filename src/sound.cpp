#include "prison/sound.hpp"
#include <SDL_error.h>
#include <SDL_timer.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

namespace prison {
SoundManager::SoundManager(const std::filesystem::path& root) {
    spec_.freq = 44100;
    spec_.format = AUDIO_S16SYS;
    spec_.channels = 1;
    spec_.samples = 1024;
    spec_.callback = mix;
    spec_.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &spec_, nullptr, 0);
    if (!device_) {
        std::cerr << "Audio unavailable: " << SDL_GetError() << '\n';
        return;
    }
    for (int i = 0; i < 6; ++i) {
        SDL_AudioSpec src;
        Uint8* bytes = nullptr;
        Uint32 size = 0;
        const char* family = i < 3 ? "default_dig_cracky" : "default_hard_footstep";
        auto path = root / "sounds/prison" / (std::string(family) + "." + std::to_string(i % 3 + 1) + ".wav");
        if (!SDL_LoadWAV(path.u8string().c_str(), &src, &bytes, &size)) {
            std::cerr << "Sound unavailable: " << path << ": " << SDL_GetError() << '\n';
            continue;
        }
        SDL_AudioCVT cvt{};
        int conversion = SDL_BuildAudioCVT(&cvt, src.format, src.channels, src.freq, spec_.format,
                                           spec_.channels, spec_.freq);
        if (conversion >= 0) {
            std::vector<Uint8> buffer(size * std::max(1, cvt.len_mult));
            std::copy(bytes, bytes + size, buffer.begin());
            cvt.buf = buffer.data();
            cvt.len = size;
            if (conversion == 0 || SDL_ConvertAudio(&cvt) == 0) {
                buffer.resize(conversion ? cvt.len_cvt : size);
                sounds_[i] = std::move(buffer);
            }
        }
        SDL_FreeWAV(bytes);
    }
    SDL_PauseAudioDevice(device_, 0);
}

SoundManager::~SoundManager() {
    if (device_)
        SDL_CloseAudioDevice(device_);
}

void SoundManager::mix(void* userdata, Uint8* stream, int length) {
    auto& audio = *static_cast<SoundManager*>(userdata);
    SDL_memset(stream, 0, length);
    for (auto& voice : audio.voices_) {
        if (!voice.sample)
            continue;
        auto count = std::min(std::size_t(length), voice.sample->size() - voice.offset);
        SDL_MixAudioFormat(stream, voice.sample->data() + voice.offset, audio.spec_.format, Uint32(count),
                           voice.volume);
        voice.offset += count;
        if (voice.offset == voice.sample->size())
            voice.sample = nullptr;
    }
}

void SoundManager::play(MiningSound sound, float volume) {
    if (!device_ || volume <= 0)
        return;
    unsigned family = sound == MiningSound::Break ? 1 : 0;
    Uint32 now = SDL_GetTicks();
    if (lastPlay_[family] && now - lastPlay_[family] < 60)
        return;
    const auto& sample = sounds_[family * 3 + nextSample_[family]++ % 3];
    if (sample.empty())
        return;
    lastPlay_[family] = now;
    float gain = sound == MiningSound::Break ? 1.f : .35f;
    SDL_LockAudioDevice(device_);
    voices_[nextVoice_++ % voices_.size()] = {&sample, 0,
                                              int(std::clamp(volume, 0.f, 1.f) * gain * SDL_MIX_MAXVOLUME)};
    SDL_UnlockAudioDevice(device_);
}
} // namespace prison
