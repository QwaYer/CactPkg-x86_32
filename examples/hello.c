/*
 * hello.c - demo application for the first CactPkg package.
 * Built as a regular CactOS ELF (dynamic, clibc.so via /lib/ld.so).
 */

#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Hello, CactOS! Package 'hello' from CactPkg %s.\n", "1.0.0");
    return 0;
}
