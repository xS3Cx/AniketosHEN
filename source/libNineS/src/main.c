#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <elf.h>
#include <signal.h>

#include "../include/proc.h"
#include "../include/ucred.h"
#include "../include/injector.h"
#include "../include/notify.h"
#include "../include/server.h"

#include "ps5/mdbg.h"

#include <dlfcn.h>

bool Inject_Toolbox(int pid, uint8_t * elf)
{                                  
    if(pid < 0 || !elf){
        notify_send("Invalid ToolBox arguments");
        return false;
    } 
    bool success = true;
    struct proc* target_proc = get_proc_by_pid(pid);//find_proc_by_name("SceShellUI");
    if (target_proc)
    {
        /* the caller reports a failure: KB+M only logs it, Toolbox notifies */
        success = inject_elf(target_proc, elf);

        free(target_proc);
    }
    else{
        return false;
    }

    return success;
}
