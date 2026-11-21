/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef AUDIOOUT_H
#define AUDIOOUT_H

#include <QAudioOutput>

#include "e-resistor.h"
#include "component.h"

class LibraryItem;

class AudioOut : public Component, public eResistor
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        virtual bool hasSimState() override { return false; }   // Paced by the sound card's buffer (wall time): no exact replay
        AudioOut( QString type, QString id );
        ~AudioOut();
    
 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void runEvent() override;

        double impedance() { return m_impedance; }
        void setImpedance( double i );

        bool buzzer() { return m_buzzer; }
        void setBuzzer( bool b );

        double frequency() { return m_frequency; }
        void setFrequency( double f ) { m_frequency = f; }
        
        virtual QPainterPath shape() const override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        virtual void slotProperties() override;
        void updtProperties();

    private:
        QAudioDeviceInfo m_deviceinfo;
        QAudioFormat     m_format;  
        
        QAudioOutput* m_audioOutput;
        QIODevice*    m_audioBuffer;
        QByteArray    m_dataBuffer;

        double m_frequency;
        double m_impedance;

        int m_dataSize;
        int m_dataCount;

        bool m_started;
        bool m_buzzer;
};

#endif


