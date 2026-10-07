#pragma once

#include "CoreMinimal.h"

class UCurveFloat;

namespace VisionStabilizerDefaultAssets
{
/** @brief Default long package name under the plugin content mount. */
inline constexpr const TCHAR* DefaultCurvePackage =
    TEXT("/VisionStabilizer/Curves/CF_VisionStabilizer_DefaultClamp");

/**
 * @brief Creates and saves a linear clamp curve, leaving existing content unchanged.
 * @param PackageName Long package name under /Game/ or /VisionStabilizer/.
 * @param OutMessage Receives the creation result or the reason for failure.
 * @return The new saved curve or an existing saved curve; null on failure or an unsaved conflict.
 * @note Must run on the game thread with a mounted, writable destination.
 */
UCurveFloat* CreateDefaultCurve(const FString& PackageName, FString& OutMessage);
}
