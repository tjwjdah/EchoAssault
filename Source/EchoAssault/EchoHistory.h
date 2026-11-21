#pragma once

#include "CoreMinimal.h"
#include "EchoPlayback.h"
#include "Algo/BinarySearch.h"

enum class EEchoWeapon : uint8 { Blaster, Seeker, Blades, Aura, End};

struct FEchoPose
{
    double Time;
    FVector Position;
    float Yaw;
};

struct FEchoShot
{
    double Time;
    FVector Origin;
    FVector Direction;
    EEchoWeapon Weapon;
    int32 Rank;
};

struct FEchoHistory
{
    TArray<FEchoPose> Poses;
    TArray<FEchoShot> Shots;
    double LastTrim = 0;

    void Trim(double Now)
    {
        if (Now - LastTrim < 1.0) return;
        LastTrim = Now;
        const double Cutoff = Now - 26.0;
        const int32 FirstPose = Algo::LowerBoundBy(Poses, Cutoff, &FEchoPose::Time);
        Poses.RemoveAt(0, FMath::Max(0, FirstPose - 1), EAllowShrinking::No);
        Shots.RemoveAt(0, Algo::LowerBoundBy(Shots, Cutoff, &FEchoShot::Time), EAllowShrinking::No);
    }

    FEchoPose PoseAt(double Time) const
    {
        check(!Poses.IsEmpty());
        const int32 Next = Algo::UpperBoundBy(Poses, Time, &FEchoPose::Time);
        if (Next == 0) return Poses[0];
        if (Next == Poses.Num()) return Poses.Last();
        const FEchoPose& A = Poses[Next - 1];
        const FEchoPose& B = Poses[Next];
        const float Alpha = (Time - A.Time) / (B.Time - A.Time);
        return {Time, FMath::Lerp(A.Position, B.Position, Alpha), A.Yaw + Alpha * FMath::FindDeltaAngleDegrees(A.Yaw, B.Yaw)};
    }

    void PathBetween(double From, double Through, TArray<FEchoPose>& Out) const
    {
        Out.Reset();
        if (Poses.IsEmpty() || Through < From || Through < Poses[0].Time || From > Poses.Last().Time) return;
        From = FMath::Max(From, Poses[0].Time);
        Through = FMath::Min(Through, Poses.Last().Time);
        Out.Add(PoseAt(From));
        for (int32 I = Algo::UpperBoundBy(Poses, From, &FEchoPose::Time); I < Poses.Num() && Poses[I].Time < Through; ++I)
        {
            Out.Add(Poses[I]);
        }
        if (Through > From) Out.Add(PoseAt(Through));
    }

    template<typename Callback>
    void Replay(double After, double Through, Callback&& Fire) const
    {
        if (!Shots.IsEmpty()) ReplayShotRange(Shots.GetData(), Shots.GetData() + Shots.Num(), After, Through, Fire);
    }
};
