
#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char** argv){
  NOB_GO_REBUILD_URSELF(argc, argv);

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, "cc", "-ggdb", "-std=gnu99", "-O0", "-o", "app", "main.c", "-lraylib");
  if(nob_cmd_run_sync_and_reset(&cmd) == 0) return 1;
  
  nob_cmd_append(&cmd, "./app");
  if(nob_cmd_run_sync_and_reset(&cmd) == 0) return 1;
  
  return 0;
}
