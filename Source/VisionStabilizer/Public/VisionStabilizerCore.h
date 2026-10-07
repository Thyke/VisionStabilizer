#pragma once

/**
 * @file VisionStabilizerCore.h
 * @brief Allocation-free math shared by the runtime node and native tests.
 *
 * No Unreal types are required, so standalone checks can exercise the same
 * implementation that evaluates the animation pose.
 */
#include <algorithm>
#include <cmath>

namespace VisionStabilizerCore
{
/** @brief Radius in centimeters used when the configured radius is non-finite. */
inline constexpr double DefaultClamp = 5.0;
/** @brief Upper speed bound in cm/s used when the configured maximum is non-finite. */
inline constexpr double DefaultMaxGroundSpeed = 600.0;
/** @brief Ranges at or below this width are treated as collapsed. */
inline constexpr double MinSpeedRange = 1.0e-8;

/** @brief Component-space position, in centimeters. */
struct FPoint
{
    double X = 0.0; ///< Position along component X.
    double Y = 0.0; ///< Position along component Y.
    double Z = 0.0; ///< Position along component Z.

    /** @return True when all three coordinates are finite. */
    bool IsFinite() const noexcept
    {
        return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z);
    }
};

/**
 * @brief Replaces non-finite input with a finite fallback, then clamps to zero.
 * @param Value Input to sanitize.
 * @param Fallback Replacement for non-finite input; a non-finite fallback becomes zero.
 * @return A finite, non-negative value.
 */
inline double NonNegativeFinite(double Value, double Fallback = 0.0) noexcept
{
    if (!std::isfinite(Value))
    {
        Value = std::isfinite(Fallback) ? Fallback : 0.0;
    }
    return (std::max)(0.0, Value);
}

/**
 * @brief Sanitizes a clamp radius and its fallback in centimeters.
 * @param Value Requested radius; negative values become zero.
 * @param Fallback Radius used for non-finite input, also sanitized before use.
 * @return A finite, non-negative radius.
 */
inline double SanitizeRadius(double Value, double Fallback = DefaultClamp) noexcept
{
    return NonNegativeFinite(Value, NonNegativeFinite(Fallback));
}

/** @brief Preserves signed height offsets; replaces non-finite offsets with zero. */
inline double SanitizeHeight(double Value) noexcept
{
    return std::isfinite(Value) ? Value : 0.0;
}

/** @brief Sanitized speed and ordered bounds, with the corresponding curve input. */
struct FSpeedSample
{
    double Speed = 0.0; ///< Non-negative ground speed in cm/s, before range clamping.
    double Minimum = 0.0; ///< Lower bound in cm/s after sanitizing and sorting.
    double Maximum = DefaultMaxGroundSpeed; ///< Upper bound in cm/s after sanitizing and sorting.
    double Normalized = 0.0; ///< Curve input in [0, 1]; zero for a collapsed range.
};

/**
 * @brief Maps ground speed to the normalized X axis of the clamp curve.
 * @param Speed Horizontal movement speed in cm/s; negative or non-finite input becomes zero.
 * @param Minimum Lower speed bound in cm/s; negative or non-finite input becomes zero.
 * @param Maximum Upper speed bound in cm/s; non-finite input uses DefaultMaxGroundSpeed.
 * @return Sanitized speed, sorted bounds and normalized speed in [0, 1].
 * @note Bounds are sorted after sanitizing. A collapsed range samples X = 0.
 */
inline FSpeedSample NormalizeGroundSpeed(
    double Speed, double Minimum, double Maximum) noexcept
{
    FSpeedSample Result;
    Result.Speed = NonNegativeFinite(Speed);
    Result.Minimum = NonNegativeFinite(Minimum);
    Result.Maximum = NonNegativeFinite(Maximum, DefaultMaxGroundSpeed);
    if (Result.Minimum > Result.Maximum)
    {
        std::swap(Result.Minimum, Result.Maximum);
    }
    const double Range = Result.Maximum - Result.Minimum;
    if (Range > MinSpeedRange)
    {
        const double Clamped = (std::clamp)(Result.Speed, Result.Minimum, Result.Maximum);
        Result.Normalized = (Clamped - Result.Minimum) / Range;
    }
    return Result;
}

/** @brief Returns Euclidean distance between two positions in centimeters. */
inline double Distance(const FPoint& A, const FPoint& B) noexcept
{
    return std::hypot(A.X - B.X, A.Y - B.Y, A.Z - B.Z);
}

/**
 * @brief Keeps a position inside or on a sphere using the shortest correction.
 * @param Previous Persistent component-space position from the last evaluation.
 * @param Center Current animated head position in component space.
 * @param Radius Allowed separation in centimeters; zero returns Center.
 * @return Previous when inside the sphere, otherwise its projection onto the surface.
 * @note Invalid centers preserve a finite Previous or return the origin. Invalid
 * previous positions and overflowing deltas return a valid Center.
 * @note This is a positional dead zone; there is no time-based interpolation.
 */
inline FPoint ProjectIntoSphere(
    const FPoint& Previous, const FPoint& Center, double Radius) noexcept
{
    if (!Center.IsFinite())
    {
        return Previous.IsFinite() ? Previous : FPoint{};
    }
    Radius = SanitizeRadius(Radius);
    if (!Previous.IsFinite() || Radius == 0.0)
    {
        return Center;
    }

    const FPoint Delta{Previous.X - Center.X, Previous.Y - Center.Y, Previous.Z - Center.Z};
    const double Length = std::hypot(Delta.X, Delta.Y, Delta.Z);
    if (!Delta.IsFinite() || !std::isfinite(Length))
    {
        // Extreme finite doubles can still overflow when subtracted. Never
        // inject a non-finite transform into the animation graph.
        return Center;
    }
    if (Length <= Radius)
    {
        return Previous;
    }

    const double Scale = Radius / Length;
    const FPoint Result{
        Center.X + Delta.X * Scale,
        Center.Y + Delta.Y * Scale,
        Center.Z + Delta.Z * Scale
    };
    return Result.IsFinite() ? Result : Center;
}

/**
 * @brief Persistent POV position, seeded from the incoming pose after each reset.
 * @note Each node owns its state. Concurrent calls on the same state require synchronization.
 */
class FPositionState
{
public:
    /** @brief Discards the seed so the next successful Step captures the incoming POV. */
    void Reset() noexcept
    {
        bInitialized = false;
        Point = {};
    }

    /** @return True after a successful Step and before the next reset or invalid input. */
    bool IsInitialized() const noexcept { return bInitialized; }
    /** @return Last filtered position, or the origin while uninitialized. */
    const FPoint& GetPoint() const noexcept { return Point; }

    /**
     * @brief Projects the persistent position into the current head-centered sphere.
     * @param IncomingPov Incoming component-space POV; used as the seed only when uninitialized.
     * @param Head Current component-space head position.
     * @param Radius Allowed separation in centimeters, sanitized before projection.
     * @return False after resetting on invalid input; the caller should pass the pose through.
     * Otherwise returns true and stores the filtered position.
     */
    bool Step(const FPoint& IncomingPov, const FPoint& Head, double Radius) noexcept
    {
        if (!IncomingPov.IsFinite() || !Head.IsFinite())
        {
            Reset();
            return false;
        }
        const FPoint Previous = bInitialized ? Point : IncomingPov;
        Point = ProjectIntoSphere(Previous, Head, Radius);
        bInitialized = true;
        return true;
    }

private:
    FPoint Point{}; ///< Filtered component-space position, without the height offset.
    bool bInitialized = false; ///< Whether Point contains a valid seed or filtered result.
};

/** @brief Curve key: Time is normalized speed; Value is clamp radius in centimeters. */
struct FDefaultCurveKey { float Time; float Value; };
/** @brief Shared keys for the generated default CurveFloat and curve tests. */
inline constexpr FDefaultCurveKey DefaultCurveKeys[] = {
    {0.0f, 2.0f}, {0.5f, 4.0f}, {1.0f, 6.0f}
};
}
