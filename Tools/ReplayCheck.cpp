// Compile with a C++17 or newer compiler. This includes the gameplay clock and event range directly.
#include "../Source/EchoAssault/EchoPlayback.h"
#include <cstdio>
#include <vector>
#include <cstdlib>

struct Shot { double Time; int Id; };
struct TestHistory
{
    struct PoseList { bool IsEmpty() const { return false; } } Poses;
    std::vector<Shot> Shots;
    template<typename Callback>
    void Replay(double A, double B, Callback Fire) const { ReplayShotRange(Shots.begin(), Shots.end(), A, B, Fire); }
};

void Expect(bool Passed, const char* Name)
{
    if (!Passed) { std::fprintf(stderr, "FAIL: %s\n", Name); std::exit(1); }
}

int main()
{
    TestHistory H;
    for (int I = 0; I <= 120; ++I)
    {
        H.Shots.push_back({I * 0.125, I*2});
        H.Shots.push_back({I * 0.125, I*2+1});
    }
    for (int FPS : {30, 60, 144})
    {
        for (int Slot : {1, 2, 3})
        {
            FEchoClock Clock;
            Clock.Delay = Slot * 8.0;
            std::vector<int> Seen;
            auto Fire = [&](const Shot& S) { Seen.push_back(S.Id); };
            double Now = 0;
            for (int Frame = 0; Now < 41; ++Frame)
            {
                const double Dt = (Frame % 113 == 0 ? 4.0 : 1.0) / FPS;
                Now += Dt;
                const double Target = Slot*8 - (Now > Slot*8+1) - (Now > Slot*8+6);
                const double Old = Clock.Previous;
                const bool WasPlaying = Clock.bStarted;
                Clock.Advance(H, Now, Dt, Target, Fire);
                if (WasPlaying) Expect(Clock.Previous >= Old && Clock.Previous-Old <= Dt*1.25+1e-8, "smooth monotonic playhead");
                const auto Count = Seen.size();
                Clock.Advance(H, Now, 0, Target, Fire);
                Expect(Seen.size() == Count, "pause does not repeat shots");
            }
            Expect(Seen.size() == H.Shots.size(), "no missing or duplicate shots");
            for (size_t I = 0; I < Seen.size(); ++I) Expect(Seen[I] == H.Shots[I].Id, "same-time order");
            std::printf("PASS %d fps / slot %d / %zu shots\n", FPS, Slot, Seen.size());
        }
    }
    FEchoClock Late;
    Late.Delay = 16;
    std::vector<int> NewShots;
    auto Collect = [&](const Shot& S) { NewShots.push_back(S.Id); };
    Late.Advance(H, 27, 1.0/60, 16, Collect);
    Expect(NewShots.size()==2 && NewShots.front()==176, "new echo starts at current playhead");
    FEchoClock Fresh;
    int ResetEvents=0;
    Fresh.Advance(H, 1, 1.0/60, 8, [&](const Shot&){ ++ResetEvents; });
    Expect(!Fresh.bStarted && ResetEvents==0, "fresh run has no active echo");
    std::puts("PASS late unlock and fresh-run clock");
}
