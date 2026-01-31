/**
 * TocinOS C Runtime Startup (crt0)
 * 
 * Entry point for dynamically linked programs.
 * Called by the dynamic linker after relocations are done.
 */

extern int main(int argc, char *argv[]);
extern void _exit(int status);

void _start(void) {
    // Get argc/argv from stack (set up by kernel/dynamic linker)
    int *sp;
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    
    // Stack layout: [argc][argv0][argv1]...[NULL][envp0]...[NULL][auxv...]
    int argc = *sp++;
    char **argv = (char **)sp;
    
    // Call main
    int ret = main(argc, argv);
    
    // Exit with return code
    _exit(ret);
    
    // Should never reach here
    while (1) __asm__ volatile("hlt");
}
