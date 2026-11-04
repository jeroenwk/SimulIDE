/***************************************************************************
 *   Instruments on the canvas: scopes, logic analyzers, voltmeters,       *
 *   frequency meters and probes, with their leads and lead tunnels, can   *
 *   be hidden to see the circuit alone. They keep measuring               *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef INSTRUMENTVIEW_H
#define INSTRUMENTVIEW_H

class Component;

class InstrumentView
{
    public:
        static InstrumentView* self();

        bool shown() { return m_shown; }
        void setShown( bool show );

        // Parts connected in parallel that only measure. Ammeters are not included: they sit
        // in series, and hiding them would leave a gap in the drawing
        static bool isInstrument( Component* comp );

        void apply();       // Show or hide every instrument, lead, lead tunnel and junction dot

    private:
        InstrumentView();

        static bool onlyLeads( Component* comp );   // Ground or rail wired only to leads

        bool m_shown;
};

#endif
