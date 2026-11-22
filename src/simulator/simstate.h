/***************************************************************************
 *   Simulation state I/O for keyframes (specs/sim-control.md §4.5)         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SIMSTATE_H
#define SIMSTATE_H

#include <cstdint>
#include <cstring>
#include <typeinfo>
#include <queue>
#include <vector>

// One function per class reads or writes its state: the same field list in both directions,
// so save and load can't drift apart:
//
//     void Diode::simState( StateIO& s ) override { eResistor::simState( s ); s.io( m_voltPN ); ... }
//
// Saving writes raw bytes straight into the keyframe arena (no allocation, no conversion);
// a write past the end sets overflow() and the caller starts again at the arena's start.
// Fields are plain values (numbers, bools, enums, fixed-size arrays). Pointers are never
// saved: they don't change during a run.
class StateIO
{
    public:
        // Saving into [buf, buf+cap)
        StateIO( uint8_t* buf, size_t cap ) : m_p( buf ), m_begin( buf ), m_end( buf+cap ), m_saving( true ) {}
        // Loading from [buf, buf+size)
        StateIO( const uint8_t* buf, size_t size, bool )
               : m_p( (uint8_t*)buf ), m_begin( (uint8_t*)buf ), m_end( (uint8_t*)buf+size ), m_saving( false ) {}

        bool saving()   { return m_saving; }
        bool overflow() { return m_over; }     // Saving: out of room; loading: read past the end
        void fail() { m_over = true; }         // Bad data while loading: the keyframe is refused
        size_t size()   { return m_p-m_begin; }

        template<class T> inline void io( T& v ) { raw( &v, sizeof( T ) ); }
        template<class T, size_t N> inline void io( T (&a)[N] ) { raw( a, sizeof( T )*N ); }

        // A queue or vector of plain values: its size, then the values
        template<class T> void ioQueue( std::queue<T>& q )
        {
            uint32_t n = q.size();
            io( n );
            if( m_saving ){
                std::queue<T> c = q;
                while( !c.empty() ){ T v = c.front(); c.pop(); io( v ); }
            }else{
                if( m_p+n*sizeof( T ) > m_end ){ m_over = true; return; }
                std::queue<T> e; q.swap( e );
                for( uint32_t i=0; i<n; ++i ){ T v; io( v ); q.push( v ); }
            }
        }
        template<class C> void ioVector( C& v )      // std::vector, QVector: plain values
        {
            typedef typename C::value_type T;
            uint32_t n = v.size();
            io( n );
            if( !m_saving ){
                if( m_p+n*sizeof( T ) > m_end ){ m_over = true; return; }
                v.resize( n );
            }
            if( n ) raw( v.data(), n*sizeof( T ) );
        }

        inline void raw( void* v, size_t n )
        {
            if( m_p+n > m_end ){ m_over = true; return; }
            if( m_saving ) memcpy( m_p, v, n );
            else           memcpy( v, m_p, n );
            m_p += n;
        }
        // A marker between sections: loading checks it, so a field list that differs between
        // save and load is caught at the next marker instead of loading garbage
        inline bool mark( uint32_t tag )
        {
            uint32_t t = tag;
            raw( &t, 4 );
            return m_saving || t == tag;
        }

    private:
        uint8_t* m_p;
        uint8_t* m_begin;
        uint8_t* m_end;
        bool m_saving;
        bool m_over = false;
};

// In a class that saves all of its own state: simState() covers it. A subclass that adds
// fields and doesn't declare its own is reported as not covered (sim history check)
#define SIM_STATE( Class ) \
    virtual bool hasSimState() override { return typeid( *this ) == typeid( Class ); }

#endif
