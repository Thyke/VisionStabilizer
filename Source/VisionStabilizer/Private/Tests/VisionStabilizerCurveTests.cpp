#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNodes/AnimNode_VisionStabilizer.h"
#include "Curves/CurveFloat.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace
{
struct FCurveTestNode : FAnimNode_VisionStabilizer
{
    using FAnimNode_VisionStabilizer::ResolveClampRadius;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionStabilizerCurveTest,
    "VisionStabilizer.Runtime.CurveSnapshotAndFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionStabilizerCurveTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FCurveTestNode Node;
    float Speed01 = 0.0f;
    bool bFallback = false;
    Node.PovClamp = 7.0f;
    TestEqual(TEXT("Manual radius"), Node.ResolveClampRadius(Speed01, bFallback), 7.0f);
    TestFalse(TEXT("Manual is not fallback"), bFallback);

    Node.ClampMode = EVisionStabilizerClampMode::GroundSpeedCurve;
    Node.PreUpdate(nullptr);
    TestEqual(TEXT("Null curve uses manual"), Node.ResolveClampRadius(Speed01, bFallback), 7.0f);
    TestTrue(TEXT("Null fallback diagnostic"), bFallback);

    TStrongObjectPtr<UCurveFloat> Curve(NewObject<UCurveFloat>());
    Node.PovClampCurve = Curve.Get();
    Node.PreUpdate(nullptr);
    TestEqual(TEXT("Empty curve uses manual"), Node.ResolveClampRadius(Speed01, bFallback), 7.0f);
    TestTrue(TEXT("Empty fallback diagnostic"), bFallback);

    for (const auto& Key : VisionStabilizerCore::DefaultCurveKeys)
    {
        const FKeyHandle Handle = Curve->FloatCurve.AddKey(Key.Time, Key.Value);
        Curve->FloatCurve.SetKeyInterpMode(Handle, RCIM_Linear);
    }
    Node.GroundSpeed = 300.0f;
    Node.PreUpdate(nullptr);
    TestEqual(TEXT("Curve midpoint"), Node.ResolveClampRadius(Speed01, bFallback), 4.0f);
    TestEqual(TEXT("Curve normalized input"), Speed01, 0.5f);
    TestFalse(TEXT("Valid curve is not fallback"), bFallback);

    Node.MinGroundSpeed = 600.0f;
    Node.MaxGroundSpeed = 0.0f;
    TestEqual(TEXT("Reversed bounds"), Node.ResolveClampRadius(Speed01, bFallback), 4.0f);
    Node.GroundSpeed = -100.0f;
    TestEqual(TEXT("Negative speed evaluates idle"), Node.ResolveClampRadius(Speed01, bFallback), 2.0f);

    // Mutate the source without PreUpdate first to verify that worker sampling
    // still sees the previous snapshot, then verify the editor refresh.
    Curve->FloatCurve.Reset();
    Curve->FloatCurve.AddKey(0.0f, 9.0f);
    TestEqual(TEXT("Worker uses owned snapshot, not changed UObject"), Node.ResolveClampRadius(Speed01, bFallback), 2.0f);
    Node.PreUpdate(nullptr);
    TestEqual(TEXT("Editor refresh copies changed keys"), Node.ResolveClampRadius(Speed01, bFallback), 9.0f);

    Curve->FloatCurve.Reset();
    Curve->FloatCurve.AddKey(0.0f, -3.0f);
    Node.PreUpdate(nullptr);
    TestEqual(TEXT("Negative curve output clamps to zero"), Node.ResolveClampRadius(Speed01, bFallback), 0.0f);

    Curve->FloatCurve.Reset();
    Curve->FloatCurve.AddKey(0.0f, std::numeric_limits<float>::quiet_NaN());
    Node.PreUpdate(nullptr);
    TestEqual(TEXT("Non-finite curve output falls back"), Node.ResolveClampRadius(Speed01, bFallback), 7.0f);
    TestTrue(TEXT("Non-finite fallback diagnostic"), bFallback);

    Node.PovClamp = std::numeric_limits<float>::infinity();
    TestEqual(TEXT("Non-finite manual fallback is safe"), Node.ResolveClampRadius(Speed01, bFallback), 5.0f);
    Node.PovClamp = -5.0f;
    TestEqual(TEXT("Negative manual fallback is zero"), Node.ResolveClampRadius(Speed01, bFallback), 0.0f);
    return true;
}

#endif
