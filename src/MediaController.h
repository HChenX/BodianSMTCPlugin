#pragma once
#include "Common.h"

namespace BodianSMTC {

class MediaController {
public:
    static void Play();
    static void Pause();
    static void PlayPause();
    static void Next();
    static void Previous();
    static void Stop();

private:
    static void SendMediaKey(WORD vk);
};

} // namespace BodianSMTC
