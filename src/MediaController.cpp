#include "MediaController.h"

namespace BodianSMTC {

void MediaController::SendMediaKey(WORD vk) {
    Log(L"SendMediaKey: 0x" + std::to_wstring(vk));
    INPUT inputs[2] = {};

    // Key Down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.dwFlags = 0;

    // Key Up
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
}

void MediaController::Play() {
    SendMediaKey(VK_MEDIA_PLAY_PAUSE);
}

void MediaController::Pause() {
    SendMediaKey(VK_MEDIA_PLAY_PAUSE);
}

void MediaController::PlayPause() {
    SendMediaKey(VK_MEDIA_PLAY_PAUSE);
}

void MediaController::Next() {
    SendMediaKey(VK_MEDIA_NEXT_TRACK);
}

void MediaController::Previous() {
    SendMediaKey(VK_MEDIA_PREV_TRACK);
}

void MediaController::Stop() {
    SendMediaKey(VK_MEDIA_STOP);
}

} // namespace BodianSMTC
