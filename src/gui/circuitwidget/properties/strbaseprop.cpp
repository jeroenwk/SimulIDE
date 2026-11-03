/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include"strbaseprop.h"

// Text values in the circuit file: valid XML character references ("&" too), so the file is
// valid XML. Older files wrote "&#x3C" etc. without the ";" and left "&" as is: still read.
QString StrBaseProp::getStr( QString str )
{
    return str.replace("&","&amp;").replace("\n","&#xa;").replace("\"","&#x22;")
            .replace("<","&#x3C;").replace("=","&#x3D;").replace(">","&#x3E;");
}

QString StrBaseProp::setStr( QString str )
{
    return str.replace("&#xa;","\n").replace("&#x22;","\"")
            .replace("&#x3C;","<").replace("&#x3D;","=").replace("&#x3E;",">")
            .replace("&#x3C","<").replace("&#x3D","=").replace("&#x3E",">")   // Older files
            .replace("&amp;","&");
}

double StrBaseProp::getValue()
{
    return getValStr().toDouble();
}
