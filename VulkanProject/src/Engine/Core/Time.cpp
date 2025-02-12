// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "Time.h"

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Time::Time()
{
    m_lastFrame = std::chrono::steady_clock::now();
}

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
void Time::update()
{
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - m_lastFrame;
    m_lastFrame = now;
    m_deltaTime = elapsed.count(); // Store the delta time
}
