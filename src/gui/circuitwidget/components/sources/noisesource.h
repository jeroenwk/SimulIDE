/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Noise source (docs/NOISE.md, "Noise source part"; NOISE_SPEC.md §11): a floating voltage
// source between + and −, through Out_Imped, whose voltage is the sum of
//   white noise     rms value, flat up to the bandwidth (a new Gaussian value every 1/(2 BW))
//   1/f noise       density at 1 Hz (V/√Hz), from a low corner up to the bandwidth: a sum of
//                   first-order (Ornstein-Uhlenbeck) processes two per decade
//   telegraph noise two levels 0 / amplitude, mean times in each (random telegraph signal)
//   hum             a sine at the mains frequency, and a DC offset
// Each is off at 0. Independent of the circuit's transient-noise setting (an explicit source,
// as ngspice TRNOISE). Seed 0: from the run seed, so a replay or a keyframe repeats it.

#ifndef NOISESOURCE_H
#define NOISESOURCE_H

#include <random>
#include <vector>

#include "component.h"
#include "e-resistor.h"

class LibraryItem;

class NoiseSource : public Component, public eResistor
{
    public:
        NoiseSource( QString type, QString id );
        ~NoiseSource();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( NoiseSource )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void runEvent() override;

        double white() { return m_white; }
        void setWhite( double v ) { m_white = v < 0 ? 0 : v; changed(); }
        double bw() { return m_bw; }
        void setBw( double v ) { m_bw = v < 1 ? 1 : v; changed(); }
        double flicker() { return m_flicker; }
        void setFlicker( double v ) { m_flicker = v < 0 ? 0 : v; changed(); }
        double fmin() { return m_fmin; }
        void setFmin( double v ) { m_fmin = v < 1e-3 ? 1e-3 : v; changed(); }
        double rtsAmp() { return m_rtsAmp; }
        void setRtsAmp( double v ) { m_rtsAmp = v; changed(); }
        double rtsHigh() { return m_rtsHigh; }
        void setRtsHigh( double v ) { m_rtsHigh = v < 1e-9 ? 1e-9 : v; changed(); }
        double rtsLow() { return m_rtsLow; }
        void setRtsLow( double v ) { m_rtsLow = v < 1e-9 ? 1e-9 : v; changed(); }
        double hum() { return m_hum; }
        void setHum( double v ) { m_hum = v; changed(); }
        double humFreq() { return m_humFreq; }
        void setHumFreq( double v ) { m_humFreq = v < 0 ? 0 : v; changed(); }
        double offset() { return m_offset; }
        void setOffset( double v ) { m_offset = v; changed(); }
        double outImp() { return m_outImp; }
        void setOutImp( double v ) { m_outImp = v < 1e-6 ? 1e-6 : v; changed(); }
        int seed() { return m_seedSet; }
        void setSeed( int s ) { m_seedSet = s < 0 ? 0 : s; changed(); }

        double voltage() { return m_volt; }       // Present source voltage (V)

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        void changed();
        void setup();
        double sample();

        double m_white, m_bw, m_flicker, m_fmin;
        double m_rtsAmp, m_rtsHigh, m_rtsLow, m_hum, m_humFreq, m_offset, m_outImp;
        int    m_seedSet;

        uint64_t m_hold;            // ps between values
        double   m_dt;              // s
        double   m_volt;
        bool     m_rtsState;
        std::mt19937_64 m_rng;
        std::normal_distribution<double> m_nd;
        std::uniform_real_distribution<double> m_ud;

        static const int MAXF = 24; // 1/f: up to 12 decades, two processes each
        int    m_nf;
        double m_fa[MAXF];          // Per step decay factors
        double m_fs[MAXF];          // Per step innovation sigmas
        double m_fx[MAXF];          // States
};

#endif
