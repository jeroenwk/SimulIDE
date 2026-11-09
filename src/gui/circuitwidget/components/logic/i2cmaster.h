/***************************************************************************
 *   I2C Master: a test bus master driven from the CLI (i2c command)        *
 *   Bug hunt IMP-023: lets the regression tests talk to TWI slave parts.   *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef I2CMASTER_H
#define I2CMASTER_H

#include <QMutex>
#include <QList>

#include "twimodule.h"
#include "iocomponent.h"

class LibraryItem;

// One transaction: START, address+W, the write bytes, (repeated) START, address+R,
// nRead bytes (ACK on all but the last), STOP. Either part may be empty.
struct I2cTransfer
{
    int addr = 0;                 // 7-bit address
    QList<uint8_t> write;
    int nRead = 0;

    // Results
    bool addrAckW = false, addrAckR = false;
    QList<bool> writeAck;         // Slave ACK per written byte
    QList<uint8_t> read;
    QStringList log;              // Bus conditions in order: "S", "A0x90+", "W0xAA+", "Sr", "R0x19", "P"
    bool done = false;
    QString error;
};

class I2CMaster : public IoComponent, public TwiModule
{
    public:
        I2CMaster( QString type, QString id );
        ~I2CMaster();

        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void runEvent() override;
        virtual void updateStep() override;
        virtual void readByte() override;

        bool pullups() { return m_pullups; }
        void setPullups( bool p );

        // CLI side (GUI thread): queue a transfer, started at the next frame; poll result()
        void request( const I2cTransfer& t );
        bool busy();
        I2cTransfer result();

    protected:
        virtual void setTwiState( twiState_t state ) override;

    private:
        enum Step { S_IDLE, S_START, S_ADDR_W, S_WRITE, S_RESTART, S_ADDR_R, S_READ, S_STOP, S_DONE };

        void nextStep();          // Simulation thread
        void finish();

        QMutex m_mutex;
        I2cTransfer m_xfer;
        bool m_queued;            // Requested, not started yet
        bool m_active;            // Running on the bus
        bool m_stateSeen;         // setTwiState() called: act after the TWI engine's event
        twiState_t m_seenState;
        Step m_step;
        int  m_idx;

        bool m_pullups;
};

#endif
