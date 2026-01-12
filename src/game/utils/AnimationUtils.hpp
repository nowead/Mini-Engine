#pragma once

#include <cmath>
#include <algorithm>

/**
 * @brief Easing functions for smooth animations
 *
 * All easing functions take a normalized time value t (0.0 to 1.0)
 * and return a normalized output value (0.0 to 1.0).
 *
 * Reference: https://easings.net/
 */
namespace AnimationUtils {

    /**
     * @brief Linear interpolation (no easing)
     * @param t Normalized time (0.0 to 1.0)
     * @return Linear output (0.0 to 1.0)
     */
    inline float linear(float t) {
        return t;
    }

    /**
     * @brief Ease in (quadratic)
     * Accelerating from zero velocity
     */
    inline float easeInQuad(float t) {
        return t * t;
    }

    /**
     * @brief Ease out (quadratic)
     * Decelerating to zero velocity
     */
    inline float easeOutQuad(float t) {
        return t * (2.0f - t);
    }

    /**
     * @brief Ease in-out (quadratic)
     * Acceleration until halfway, then deceleration
     */
    inline float easeInOutQuad(float t) {
        if (t < 0.5f) {
            return 2.0f * t * t;
        } else {
            return -1.0f + (4.0f - 2.0f * t) * t;
        }
    }

    /**
     * @brief Ease in (cubic)
     * Stronger acceleration
     */
    inline float easeInCubic(float t) {
        return t * t * t;
    }

    /**
     * @brief Ease out (cubic)
     * Stronger deceleration
     */
    inline float easeOutCubic(float t) {
        float f = t - 1.0f;
        return f * f * f + 1.0f;
    }

    /**
     * @brief Ease in-out (cubic)
     * Smooth acceleration and deceleration
     */
    inline float easeInOutCubic(float t) {
        if (t < 0.5f) {
            return 4.0f * t * t * t;
        } else {
            float f = 2.0f * t - 2.0f;
            return 0.5f * f * f * f + 1.0f;
        }
    }

    /**
     * @brief Ease out elastic
     * Overshoots and oscillates (like a spring)
     */
    inline float easeOutElastic(float t) {
        if (t == 0.0f || t == 1.0f) {
            return t;
        }

        const float p = 0.3f;
        return std::pow(2.0f, -10.0f * t) *
               std::sin((t - p / 4.0f) * (2.0f * 3.14159f) / p) + 1.0f;
    }

    /**
     * @brief Ease out bounce
     * Bounces at the end
     */
    inline float easeOutBounce(float t) {
        if (t < 1.0f / 2.75f) {
            return 7.5625f * t * t;
        } else if (t < 2.0f / 2.75f) {
            float f = t - 1.5f / 2.75f;
            return 7.5625f * f * f + 0.75f;
        } else if (t < 2.5f / 2.75f) {
            float f = t - 2.25f / 2.75f;
            return 7.5625f * f * f + 0.9375f;
        } else {
            float f = t - 2.625f / 2.75f;
            return 7.5625f * f * f + 0.984375f;
        }
    }

    /**
     * @brief Interpolate between two values using an easing function
     * @param start Start value
     * @param end End value
     * @param t Normalized time (0.0 to 1.0)
     * @param easingFunc Easing function to use
     * @return Interpolated value
     */
    template<typename T>
    inline T lerp(T start, T end, float t, float (*easingFunc)(float) = linear) {
        float easedT = easingFunc(std::clamp(t, 0.0f, 1.0f));
        return start + (end - start) * easedT;
    }

    /**
     * @brief Default easing function for height animations
     * Smooth acceleration and deceleration
     */
    inline float defaultHeightEasing(float t) {
        return easeInOutCubic(t);
    }

    /**
     * @brief Easing function for surge effects (rocket launch)
     * Quick acceleration with elastic overshoot
     */
    inline float surgeEasing(float t) {
        return easeOutElastic(t);
    }

    /**
     * @brief Easing function for crash effects (building burial)
     * Smooth acceleration into the ground
     */
    inline float crashEasing(float t) {
        return easeInCubic(t);
    }

} // namespace AnimationUtils
