#pragma once
#include <chrono>

class Time
{
public:
    Time();
    void update();

    float getDeltaTime() const { return m_deltaTime; }

private:
    float m_deltaTime = 0.0f;
    std::chrono::steady_clock::time_point m_lastFrame;
};