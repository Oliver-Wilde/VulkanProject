#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <chrono>

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class Time
{
public:
    // -----------------------------------------------------------------------------
    // Constructor
    // -----------------------------------------------------------------------------
    Time();

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Updates the internal timing, calculating delta time since last frame.
     */
    void update();

    /**
     * Retrieves the time elapsed (in seconds) since the last frame.
     *
     * @return Delta time as a float.
     */
    float getDeltaTime() const { return m_deltaTime; }

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    float m_deltaTime = 0.0f; ///< Time in seconds since the last frame

    /**
     * Stores the point in time when the last frame occurred,
     * used to calculate delta time in the update() method.
     */
    std::chrono::steady_clock::time_point m_lastFrame;
};
