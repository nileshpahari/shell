#ifndef EXEC_H
#define EXEC_H

#include "./parser.h"

void execute_command(command_t cmd);
void execute(pipeline_t pipeline);

#endif
