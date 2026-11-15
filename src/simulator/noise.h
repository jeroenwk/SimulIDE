/***************************************************************************
 *   Transient noise: physical noise of the parts in a time simulation     *
 *   (spec: docs/NOISE_SPEC.md)                                            *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef NOISE_H
#define NOISE_H

#include <atomic>
#include <functional>
#include <random>
#include <vector>

#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

#include "e-element.h"

class ePin;
class eReactive;

namespace Noise
{
    constexpr double kB = 1.380649e-23;    // J/K
    constexpr double q  = 1.602176634e-19; // C
    constexpr double T  = 300.15;          // K: the same 27 °C as Vt = 25.865 mV in the models
    constexpr double fourKT = 4*kB*T;
    constexpr double twoQ   = 2*q;
}

// One noise current source between the nodes of two pins of a part (spec §5.2)
struct NoiseSrc
{
    enum Kind { Thermal=0, Shot, Channel };

    ePin* a = nullptr;           // The part's pins: the source sits between their nodes,
    ePin* b = nullptr;           // its current flows from a's node into b's node
    Kind  kind = Thermal;
    std::function<double()> psd; // One-sided S in A²/Hz at the present operating point
    QString elementId;           // Id of the element that gave it (owner found from it)
    QString partId;              // Label of the part (subcircuit parts: "U1-R2")
    QString partType;            // Component type of the part (library name)

    static QString kindName( Kind k );
};

// Circuit-level settings (spec §3.1), kept by the Simulator and saved with the circuit
struct NoiseSettings
{
    bool        on    = false;
    double      bw    = 10e6;      // Hz
    double      scale = 1;
    uint64_t    seed  = 0;         // 0: random at every start
    uint64_t    start = 0;         // ps
    uint64_t    stop  = 0;         // ps, 0 = until the end of the run
    QStringList off;               // Labels, uids or "type:NAME"

    bool isDefault() const;
    uint64_t holdPs() const;       // Δt = 1/(2·bw) in ps, at least 1
};

class eNoise : public eElement
{
    public:
        eNoise( const NoiseSettings& s, uint64_t seed );
        ~eNoise();

        virtual void stamp() override;
        virtual void runEvent() override;

        enum State { Armed=0, Active, Ended };
        State state() { return (State)m_state.load(); }

        int sources() { return (int)m_src.size(); }
        int count( NoiseSrc::Kind k );
        QStringList unmatched() { return m_unmatched; }

        // Seeded random start state (0/1) of a logic element, -1 if its part is in noiseOff
        int startState( const QString& elId );

        struct SrcInfo { QString part, type, kind, nodeA, nodeB; double psd, rms; };
        QList<SrcInfo> info();     // GUI thread, for the CLI `noise sources`

        uint64_t firstActive() { return m_firstActive; }   // ps, 0 = never
        uint64_t lastActive()  { return m_lastActive; }    // ps of the last update
        uint64_t activeUntil();    // ps: the window's stop once ended, else the end of the last hold

    private:
        void collect();
        void zero();
        void fineSteps( bool on );   // Capacitors and inductors follow the noise step in the window
        std::vector<eReactive*> m_reactive;

        struct Src {
            NoiseSrc s;
            ePin* pa; ePin* pb;    // Own pins on the part's nodes (spec §5.1)
            std::mt19937_64 rng;
            std::normal_distribution<double> nd;
            double last = 0;       // Last current (A)
            double lastPsd = 0;
        };
        std::vector<Src*> m_src;

        NoiseSettings m_set;
        uint64_t m_seed;
        uint64_t m_hold;
        double   m_gain;           // scale·√bw
        QStringList m_unmatched;
        QStringList m_startUsed;   // noiseOff entries matched by startState()
        QHash<QString, QString> m_startOff;   // Part uid → its noiseOff entry ("" = none), built once
        bool m_startMapped = false;

        std::atomic<int> m_state;
        std::atomic<uint64_t> m_firstActive;
        std::atomic<uint64_t> m_lastActive;
};

#endif
