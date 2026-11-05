/***************************************************************************
 *   Export the circuit as an image (PNG or SVG), with an optional colour  *
 *   theme, with or without instruments and grid. Used by the Export      *
 *   dialog and the CLI "export" command                                   *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CIRCUITEXPORT_H
#define CIRCUITEXPORT_H

#include <QString>
#include <QRectF>
#include <QSize>

struct ExportOptions
{
    QString format = "png";  // png or svg
    QString theme;           // Colour preset (Classic, Light, Dark); empty = the current colours
    bool    meters = true;   // Instruments (scopes, meters, probes) with their leads
    bool    grid   = false;
    double  scale  = 2;      // PNG pixels per scene unit
    double  margin = 24;     // Scene units around the drawing
};

struct ExportResult
{
    QString file;
    QRectF  area;            // Scene area exported
    QSize   size;            // Image size (pixels for PNG, user units for SVG)
    double  scale = 1;
};

namespace CircuitExport
{
    // Throws nothing; returns false and sets error on failure
    bool write( QString file, const ExportOptions& opt, ExportResult* res, QString* error );

    QString formatOf( QString file );   // "png", "svg" or "" from the extension
}

#endif
