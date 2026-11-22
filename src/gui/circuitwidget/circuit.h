/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CIRCUIT_H
#define CIRCUIT_H

#include <QGraphicsScene>
#include <QTimer>

#include "component.h"
#include "connector.h"
#include "pin.h"

#define COMP_STATE_NEW "__COMP_STATE_NEW__"

class CircuitView;
class SubPackage;
class Simulator;
class SubCircuit;
class Node;
class Triggers;
class ConnectorLine;

class Circuit : public QGraphicsScene
{
    friend class SubCircuit;
    friend class Simulator;

    Q_OBJECT

    public:
        Circuit( qreal x, qreal y, qreal width, qreal height, CircuitView* parent );
        ~Circuit();

 static Circuit* self() { return m_pSelf; }

        bool drawGrid() { return !m_hideGrid; }
        void setDrawGrid( bool draw );
        void setGridShown( bool show ) { m_hideGrid = !show; }   // Not saved (image export)

        // Wire bands (specs/wire-colors.md), drawn in the background below every item:
        // bandsChanged() after a band or a banded wire changes (repaints soon, once)
        void   bandsChanged();
        QRectF bandsRect();               // Where bands are drawn (exports add it)

        bool animate() { return m_animate; }
        // Charge display reference voltage (V), 0 = auto (largest source voltage)
        double chargeScale() { return m_chargeScale; }
        void setChargeScale( double v );
        double currentScale() { return m_currentScale; }   // Inductor gauges 100 % (A), 0 = auto
        void setCurrentScale( double a );

        // What the circuit shows and how to use it (Markdown), saved as <description> in the file
        QString description() { return m_description; }
        void setDescription( QString d );
        static QString descriptionFromDoc( const QString &doc );
        void setAnimate( bool an );

        int autoBck();
        void setAutoBck( int secs );

        void removeItems();

        // Move the selection (parts, junctions, wire segments) and the wires attached to it,
        // as a mouse drag does. newStep: the start of a move, the old positions are saved
        // as one undo step (later calls of the same move add nothing to undo)
        void moveSelection( QPointF delta, bool newStep );
        void removeComp( Component* comp) ;
        void compRemoved( bool removed ) { m_compRemoved = removed; }
        void removeNode( Node* node );
        void removeConnector( Connector* conn );
        void clearCircuit();

        //--- Undo/Redo ----------------------------------
        void saveChanges();
        void removeLastUndo();
        void addCompChange(  QString component, QString property, QString undoVal );
        void saveCompChange( QString component, QString property, QString undoVal );
        void beginCircuitBatch();
        void endCircuitBatch();
        void calcCircuitChanges(); // Calculate total changes
        void cancelUndoStep();     // Revert changes done
        void beginUndoStep();      // Record current state
        void endUndoStep();        // Does create/remove
        bool undoRedo() { return m_undo || m_redo; }
        //------------------------------------------------

        void setChanged();

        // Text components with a font other than the bundled ones get Inter (JetBrains Mono
        // for monospace names). One undo step. Returns the number of components changed
        int convertFontsToDefault();

        void accepKeys( bool a ) { m_acceptKeys = a; }

        Pin* findPin( int x, int y, QString id );

        void loadCircuit( QString fileName );
        bool saveCircuit( QString fileName );
        QString toXml() { return circuitToString(); } // Circuit file contents (used by CLI)

        Triggers* triggers() { return m_triggers; }   // Its triggers (specs/triggers.html), saved with it

        Component* createItem( QString name, QString id, bool map=true );

        QString newSceneId() { return QString::number(++m_seqNumber); }
        QString newConnectorId() { return QString::number(++m_conNumber); }

        void newconnector( Pin* startpin, bool save=true );
        void closeconnector( Pin* endpin, bool save=false );
        void deleteNewConnector();
        Connector* getNewConnector() { return m_newConnector; }

        // Wire two pins, as the CLI wire add and the keyboard "_" key do (K8, specs/
        // keyboard-input.md): a pin that already has a wire gets a junction dot on it,
        // H/V route with the corner chosen by the pins' sides. One undo step; the
        // simulation is powered off first, as for a mouse wire.
        // nullptr: same pin, bus mismatch, already the same wire, or no wire segment
        // of the net long enough for the junction
        Connector* connectPins( Pin* pinA, Pin* pinB );

        // Wire helpers, shared by the CLI wire add and connectPins
        static bool splittable( ConnectorLine* line );          // Straight, long enough to split
        static QList<Connector*> netWires( Connector* first );  // The net's wires, through junctions
        static int backwards( QString side, QPoint d );         // A leg against the way a pin faces
        static QStringList routePoints( QPoint a, QPoint b, QList<QPoint> via, bool startVertical );
        static bool routeStartsVertical( Pin* sp, Pin* ep );    // Where the one corner turns
        // Where a junction for a wired pin goes (segment + point); ownWire false: on another
        // wire of its net (its own is too short). nullptr: no wire, or none long enough
        static ConnectorLine* junctionLine( Pin* pin, QPoint* at, bool* ownWire = nullptr );

        void addNode( Node* node );
        void addComponent( Component* comp );

        QList<Component*>* compList() { return &m_compList; }
        QList<Connector*>* conList()  { return &m_connList; }
        QList<Node*>*      nodeList() { return &m_nodeList; }
        QHash<QString, CompBase*>* compMap() { return &m_compMap;}

        Component* getCompById( QString id );
        QString origId( QString name ) { return m_idMap.value( name ); } // used by Shield

        bool is_constarted() { return m_conStarted ; }

        SubPackage* getBoard() { return m_board; }
        void setBoard( SubPackage* b ) { m_board = b; }

        SubCircuit* getSubcircuit() { return m_subCircuit; }
        void setSubcircuit( SubCircuit* sc ) { m_subCircuit = sc; }

        bool pasting() { return m_pasting; }
        bool isBusy()  { return m_busy || m_pasting | m_deleting; }
        //bool isSubc()  { return m_createSubc; }

        void addPin( Pin* pin, QString pinId ) { m_pinMap[ pinId ] = pin; m_LdPinMap[ pinId ] = pin; }
        void remPin( QString pinId ) { m_pinMap.remove( pinId ); }
        Pin* pinById( QString pinId ) { return m_pinMap.value( pinId ); }
        bool loading() { return m_loading; }
        int  undoIndex() { return m_undoIndex; }
        bool restoring() { return m_undo || m_redo; }     // Undo / redo is setting properties
        void joinUndoSteps( int index );     // The steps saved after index become one
        void updatePin( ePin* epin, QString oldId, QString newId );

        QString getSeqNumber( QString name );
        QString replaceId( QString pinName );

        const QString getFilePath() const { return m_filePath; }
        void setFilePath( QString f ) { m_filePath = f; }

        void drawBackground( QPainter* painter, const QRectF &rect );
        void drawForeground( QPainter* painter, const QRectF &rect ) override;

    signals:
        void keyEvent( QString key, bool pressed );

    public slots:
        void copy( QPointF eventpoint );
        void paste( QPointF eventpoint );
        void undo();
        void redo();
        bool canUndo() { return m_undoIndex >= 0; }
        bool canRedo() { return m_undoIndex < m_undoStack.size()-1; }
        int  editCount() { return m_editCount; }  // +1 for every undo step saved, undo and redo
        void importCircuit();
        //void bom();
        void saveBackup();

    protected:
        void mousePressEvent( QGraphicsSceneMouseEvent* event );
        void mouseReleaseEvent( QGraphicsSceneMouseEvent* event );
        void mouseMoveEvent( QGraphicsSceneMouseEvent* event );
        bool event( QEvent* event ) override;     // Tab / Shift+Tab to the keyboard controller
        void keyPressEvent( QKeyEvent* event );
        void keyReleaseEvent( QKeyEvent* event );
        void dropEvent( QGraphicsSceneDragDropEvent* event );

    private:
 static Circuit*  m_pSelf;

        void loadStrDoc( QString &doc );
        bool saveString( QString &fileName, QString doc );
        QString circuitHeader();
        QString descriptionToXml();
        QString circuitToString();

        void updatePinName( QString* name );

        QString m_filePath;
        QString m_backupPath;

        QRect        m_scenerect;
        CircuitView* m_graphicView;
        Connector*   m_newConnector;
        CompBase*    m_newComp;

        int m_seqNumber;
        int m_conNumber;
        int m_error;

        bool m_pasting;
        bool m_deleting;
        bool m_loading;
        bool m_conStarted;
        bool m_hideGrid;
        bool   m_bandsPending = false;
        QRectF m_bandsRect;               // Last repainted band area
        void   drawBands( QPainter* painter );

        bool m_compRemoved;
        bool m_animate;
        double m_chargeScale;
        double m_currentScale;
        QString m_description;
        bool m_changed;
        bool m_busy;
        bool m_undo;
        bool m_redo;
        bool m_acceptKeys;
        //bool m_createSubc;
        int m_cicuitBatch;
        int m_editCount;

        QPointF m_eventpoint;
        QPointF m_deltaMove;

        QList<Component*> m_compList;   // Component list
        QList<Connector*> m_connList;   // Connector list
        QList<Node*>      m_nodeList;   // Node list

        SubPackage* m_board;
        SubCircuit* m_subCircuit;

        QHash<QString, Pin*>      m_pinMap;   // Pin Id to Pin*
        QHash<QString, Pin*>      m_LdPinMap; // Pin Id to Pin* while loading/pasting/importing
        QHash<QString, QString>   m_idMap;    // Component seqNumber to new seqNumber (pasting)
        QHash<QString, CompBase*> m_compMap;  // Component Id to Component*

        QTimer m_bckpTimer;

        Simulator* m_simulator;
        Triggers*  m_triggers;
        QHash<QString, QString> m_pasteUids;   // Pasting: old uid -> new uid (their triggers follow)

        //--- Undo/Redo ----------------------------------

        struct compChange{      // Component Change to be performed by Undo/Redo to complete a Circuit change
            QString component;  // Component name
            QString property;   // Property name
            QString undoValue;  // Property value for Undo step
            QString redoValue;  // Property value for Redo step
        };
        struct circChange{       // Circuit Change to be performed by Undo/Redo to restore circuit state
            QList<compChange> compChanges;
            int size() { return compChanges.size(); }
            void clear() { compChanges.clear(); }
        };

        inline void clearCircChanges() { m_circChange.clear(); }
        void deleteRemoved();
        void restoreState();

        int m_maxUndoSteps;
        int m_undoIndex;

        circChange m_circChange;
        QList<circChange> m_undoStack;

        QList<CompBase*>  m_removedComps; // removed component list;
        QList<Connector*> m_oldConns;
        QList<Component*> m_oldComps;
        QList<Node*>      m_oldNodes;
        QMap<CompBase*, QString> m_compStrMap;
};

#endif
