#include "EchoHistory.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoTimelineTest, "EchoAssault.Replay.Timeline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoTimelineTest::RunTest(const FString&)
{
    for (int32 FPS : {30, 60, 144})
    {
        FEchoHistory H;
        H.Poses = {{0, FVector::ZeroVector, 170}, {1, FVector(100, 0, 0), -170}};
        const FEchoPose Mid = H.PoseAt(0.5);
        TestEqual(TEXT("interpolated position"), Mid.Position.X, 50.0);
        TestEqual(TEXT("short yaw arc"), Mid.Yaw, 180.f);
        for (int I = 0; I < 20; ++I)
        {
            H.Shots.Add({I * 0.25, FVector::ZeroVector, FVector::ForwardVector, EEchoWeapon::Blaster, I});
        }
        H.Shots.Add({4.75, FVector::ZeroVector, FVector::RightVector, EEchoWeapon::Seeker, 3});
        for (int32 Slot = 1; Slot <= 3; ++Slot)
        {
            FEchoClock Clock;
            Clock.Delay = Slot * 8;
            TArray<int32> Seen;
            auto Record = [&](const FEchoShot& Shot) { Seen.Add(Shot.Rank); };
            for (int32 Frame = 0; Frame <= FPS * 31; ++Frame)
            {
                const double Now = double(Frame) / FPS;
                const double Target = Slot * 8 - (Now > Slot * 8 + 1 ? 1 : 0);
                Clock.Advance(H, Now, 1.0 / FPS, Target, Record);
                Clock.Advance(H, Now, 0, Target, Record);
            }
            TestEqual(TEXT("exact event count including simultaneous shots"), Seen.Num(), 21);
            for (int32 I = 0; I < FMath::Min(20, Seen.Num()); ++I) TestEqual(TEXT("snapshot order"), Seen[I], I);
        }
        H.Trim(40);
        TestEqual(TEXT("one interpolation anchor"), H.Poses.Num(), 1);
        TestTrue(TEXT("expired events removed"), H.Shots.IsEmpty());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEchoTrailPathTest, "EchoAssault.Replay.TrailPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEchoTrailPathTest::RunTest(const FString&)
{
    TArray<FEchoPose> Path;
    FEchoHistory Empty;
    Path.Add({0, FVector::ZeroVector, 0});
    Empty.PathBetween(0, 8, Path);
    TestTrue(TEXT("empty or restarted history clears the displayed path"), Path.IsEmpty());

    FEchoHistory Corner;
    Corner.Poses = {{0, FVector::ZeroVector, 0}, {2, FVector(200, 0, 50), 0},
                    {3, FVector(200, 100, 50), 90}, {10, FVector(900, 100, 50), 90}};
    Corner.PathBetween(1.5, 3.5, Path);
    if (!TestEqual(TEXT("clipped trail retains both corner samples"), Path.Num(), 4)) return false;
    TestTrue(TEXT("old endpoint interpolates the actual replay position"), Path[0].Position.Equals(FVector(150, 0, 37.5)));
    TestTrue(TEXT("turn is not replaced by a straight chord"), Path[2].Position.Equals(FVector(200, 100, 50)));
    TestTrue(TEXT("current endpoint is interpolated"), Path.Last().Position.Equals(FVector(250, 100, 50)));
    Corner.PathBetween(-8, 1, Path);
    TestEqual(TEXT("startup never draws before the first recorded pose"), Path[0].Time, 0.0);
    Corner.PathBetween(4, 3, Path);
    TestTrue(TEXT("reversed interval has no path"), Path.IsEmpty());

    for (int32 FPS : {30, 60, 144})
    {
        FEchoHistory H;
        for (int32 Frame = 0; Frame <= FPS * 22; ++Frame)
        {
            const double Time = double(Frame) / FPS;
            H.Poses.Add({Time, FVector(Time * 100, 0, 50), 0});
        }
        FEchoClock Clock;
        for (int32 Frame = 0; Frame <= FPS * 22; ++Frame)
        {
            const double Now = double(Frame) / FPS;
            const double Target = Now < 10 ? 8 : (Now < 15 ? 7 : 6);
            Clock.Advance(H, Now, 1.0 / FPS, Target, [](const FEchoShot&) {});
            if (!Clock.bStarted) continue;
            H.PathBetween(Clock.Previous, Now, Path);
            if (Path.IsEmpty()) { AddError(TEXT("active clock must have a path")); return false; }
            if (!FMath::IsNearlyEqual(Path[0].Time, Now - Clock.Delay, 1.e-8) ||
                !Path[0].Position.Equals(FVector(Clock.Previous * 100, 0, 50), 1.e-6) ||
                !FMath::IsNearlyEqual(Path.Last().Time, Now, 1.e-8))
            {
                AddError(TEXT("trail must follow the real clock throughout both acceleration transitions"));
                return false;
            }
        }
        TestTrue(TEXT("both acceleration stages settle at a six second path"), FMath::IsNearlyEqual(Clock.Delay, 6.0, 1.e-8));
        const auto Frozen = Path;
        H.PathBetween(Clock.Previous, 22, Path);
        TestEqual(TEXT("unchanged game time keeps the paused path length"), Path.Num(), Frozen.Num());
        TestEqual(TEXT("paused path keeps its oldest time"), Path[0].Time, Frozen[0].Time);
    }
    return true;
}
#endif
