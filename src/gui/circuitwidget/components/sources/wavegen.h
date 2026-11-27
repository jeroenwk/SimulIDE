/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef WAVEGEN_H
#define WAVEGEN_H

#include "clock-base.h"

class LibraryItem;

class WaveGen : public ClockBase
{
    public:
        WaveGen( QString type, QString id );
        ~WaveGen();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( WaveGen )

        // K7: typed values set the Freq (not shown by default: an empty ShowProp is
        // not saved, so a shown default would show up on every older circuit)
        QString valueProperty() override { return "Freq"; }

        enum waveType_t {
            Sine = 0,
            Saw,
            Triangle,
            Square,
            Random,
            Wav
        };

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual bool setPropStr( QString prop, QString val ) override;

        virtual void stamp() override;
        virtual void runEvent() override;

        double duty() { return m_duty; }
        void setDuty( double duty );

        int steps() { return m_steps; }
        void setSteps( int steps );

        double phaseShift() { return m_phaseShift; }
        void setPhaseShift( double p ) { m_phaseShift = p; }

        QString waveType() { return m_waveTypeStr; }
        void setWaveType( QString type );

        double semiAmpli() { return m_voltage/2; }
        void setSemiAmpli( double v );

        double midVolt() { return m_voltMid; }
        virtual double nominalVolt() override
        {   if( m_bipolar ) return qAbs( m_voltMid )+m_voltage/2;
            return qMax( qAbs( m_voltBase ), qAbs( m_voltBase+m_voltage ) ); }
        void setMidVolt( double v );

        QString fileName() { return m_fileName; }
        void setFile( QString fileName );

        bool bipolar() { return m_bipolar; }
        void setBipolar( bool b );

        bool floating() { return m_floating; }
        void setFloating( bool f );

        double outImp() { return m_outImp; }
        void setOutImp( double imp );

        virtual void updateStep() override;

        virtual void setFreq( double freq ) override;

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )override;
        
    public slots:
        void slotLoad();
        virtual void contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) override;

    protected slots:
        virtual void slotProperties() override;

    private:
        void genSine();
        void genSaw();
        void genTriangle();
        void genSquare();
        void genRandom();
        void genWav();
        inline uint64_t remainerInt();

        void updtProperties();

        double normalize( double data );
        
        void applyOutImp();
        void levelChanged();

        bool m_bipolar;
        bool m_floating;
        bool m_impChanged;
        bool m_levelChanged;
        double m_phaseOff;     // ps added to the time since m_lastTime: the phase kept across a frequency change (#644)
        double m_runPeriod;    // the period the running wave used (0: not running yet)   // a level or shape set during a run: re-evaluated at the next frame (#773)

        double m_outImp;   // Output impedance (create-circuit #386), between Out and Gnd/ground

        QString m_waveTypeStr;
        waveType_t m_waveType;

        double m_duty;
        double m_vOut;
        double m_voltMid;
        double m_voltBase;
        double m_lastVout;
        double m_halfW;
        double m_time;
        double m_phaseShift;
        
        int      m_steps;
        uint64_t m_qSteps;
        uint64_t m_nextStep;

        uint m_index;
        uint16_t m_audioFormat;
        uint16_t m_numChannels;
        uint32_t m_sampleRate;
        uint16_t m_blockSize;
        uint16_t m_bitsPerSample;

        double m_maxValue;
        double m_minValue;
        double m_mult;
        std::vector<double> m_data;
        QString m_fileName;

        IoPin* m_gndpin;

        QStringList m_waves;
        QPixmap* m_wavePixmap;
};

#endif
