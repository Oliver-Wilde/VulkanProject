#include "Time.h"

Time::Time() {
    m_lastFrame = std::chrono::steady_clock::now();
}

void Time::update() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - m_lastFrame;
    m_lastFrame = now;
    m_deltaTime = elapsed.count(); // store the delta
}