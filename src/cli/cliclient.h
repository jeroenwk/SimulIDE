/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CLICLIENT_H
#define CLICLIENT_H

// Client mode: "simulide cli [options] <command...>"
// Sends commands to a running SimulIDE (GUI or --headless) and prints the replies.
// Returns the process exit code.
int runCliClient( int argc, char* argv[] );

#endif
