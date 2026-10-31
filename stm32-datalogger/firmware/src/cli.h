/* Serial command line: collects typed characters and runs a command on Enter. */
#ifndef CLI_H
#define CLI_H

void cli_poll(void);        /* CliTask: waits for one key, then handles it (a whole line on Enter) */

#endif
