/* main.c - CactPkg entry point: command-line parsing and dispatch. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cactpkg.h"

#define CONF_DEFAULT "/etc/cactpkg.conf"

static const char *usage_text =
    "CactPkg " CP_VERSION " - CactOS package manager (" CP_ARCH_DEFAULT ")\n"
    "\n"
    "Usage:\n"
    "  cactpkg [global-flags] <command> [arguments...]\n"
    "\n"
    "Commands:\n"
    "  update                refresh indexes of configured repositories\n"
    "  install <package...>  install packages from a repository (with dependencies)\n"
    "  install <file.cpkg>   install a package from a file\n"
    "  remove <package...>   remove installed packages\n"
    "  list                  list installed packages\n"
    "  search <pattern>      search available packages\n"
    "  info <package>        show package details\n"
    "  files <package>       files of an installed package\n"
    "  help                  show this text\n"
    "  version               show version\n"
    "\n"
    "Global flags:\n"
    "  -y                     assume yes on prompts\n"
    "  -f, --force            force (reinstall / bypass checks)\n"
    "  -q, --quiet            quiet output\n"
    "  -R, --repo <dir>       add a repository directory (repeatable)\n"
    "      --prefix <dir>     installation root (default from config)\n"
    "  -C, --conf <file>     config file (default " CONF_DEFAULT ")\n";

static void cmd_help(void)
{
    fputs(usage_text, stdout);
}

static void cmd_version(void)
{
    cp_out("CactPkg %s (arch %s)\n", CP_VERSION, CP_ARCH_DEFAULT);
}

int main(int argc, char **argv)
{
    struct cp_config cfg;
    cfg_defaults(&cfg);

    const char *conf_path = CONF_DEFAULT;
    const char *prefix_ovr = NULL;
    int force = 0, yes = 0, quiet = 0;

    char *cmd = NULL;
    char *args[64];
    int nargs = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            cmd_help();
            return 0;
        }
        if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0 ||
            (strcmp(a, "version") == 0 && !cmd)) {
            cmd_version();
            return 0;
        }
        if (strcmp(a, "-y") == 0) {
            yes = 1;
            continue;
        }
        if (strcmp(a, "-f") == 0 || strcmp(a, "--force") == 0) {
            force = 1;
            continue;
        }
        if (strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0) {
            quiet = 1;
            continue;
        }
        if (strcmp(a, "-R") == 0 || strcmp(a, "--repo") == 0 ||
            strcmp(a, "--prefix") == 0 ||
            strcmp(a, "-C") == 0 || strcmp(a, "--conf") == 0) {
            if (i + 1 >= argc) {
                cp_err("cactpkg: %s requires an argument\n", a);
                return 1;
            }
            const char *val = argv[++i];
            if (strcmp(a, "-R") == 0 || strcmp(a, "--repo") == 0)
                cfg_add_repo(&cfg, val);
            else if (strcmp(a, "--prefix") == 0)
                prefix_ovr = val;
            else
                conf_path = val;
            continue;
        }
        if (a[0] == '-' && a[1] != '\0') {
            cp_err("cactpkg: unknown flag: %s\n", a);
            return 1;
        }

        if (!cmd)
            cmd = (char *)a;
        else if (nargs < 64)
            args[nargs++] = (char *)a;
    }

    /* load config after collecting flags so that -C is honoured */
    if (cfg_load(&cfg, conf_path) != 0) {
        cp_err("cactpkg: error in config file %s\n", conf_path);
        return 1;
    }
    if (prefix_ovr)
        strncpy(cfg.prefix, prefix_ovr, CP_PATH_MAX - 1);

    if (!cmd) {
        cmd_help();
        return 1;
    }

    if (strcmp(cmd, "help") == 0) {
        cmd_help();
        return 0;
    }
    if (strcmp(cmd, "update") == 0)
        return cmd_update(&cfg) == 0 ? 0 : 1;
    if (strcmp(cmd, "install") == 0)
        return cmd_install(&cfg, nargs, args, force, yes, quiet) == 0 ? 0 : 1;
    if (strcmp(cmd, "remove") == 0)
        return cmd_remove(&cfg, nargs, args, force, quiet) == 0 ? 0 : 1;
    if (strcmp(cmd, "list") == 0)
        return cmd_list(&cfg);
    if (strcmp(cmd, "search") == 0) {
        if (nargs < 1) {
            cp_err("usage: cactpkg search <pattern>\n");
            return 1;
        }
        return cmd_search(&cfg, args[0]);
    }
    if (strcmp(cmd, "info") == 0) {
        if (nargs < 1) {
            cp_err("usage: cactpkg info <package>\n");
            return 1;
        }
        return cmd_info(&cfg, args[0]);
    }
    if (strcmp(cmd, "files") == 0) {
        if (nargs < 1) {
            cp_err("usage: cactpkg files <package>\n");
            return 1;
        }
        return cmd_files(&cfg, args[0]);
    }
    if (strcmp(cmd, "version") == 0) {
        cmd_version();
        return 0;
    }

    cp_err("cactpkg: unknown command: %s\n", cmd);
    return 1;
}
