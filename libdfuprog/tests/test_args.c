/*
 * libdfuprog unit test: command-line splitting and the no-device path.
 *
 * Build and run with `make -C libdfuprog/tests` (needs libusb-1.0 headers).
 * Exercises the fix for espotek-org/Labrador#450, where the firmware hex path
 * "C:\Program Files\EspoTek Labrador\..." was split at its spaces.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdfuprog.h"

static int failures = 0;

static void check_split(const char *line, int expected_argc, const char **expected)
{
    dfuprog_args args;
    int argc = dfuprog_split_args(line, &args);
    int i;

    if (argc != expected_argc) {
        printf("FAIL: [%s] -> argc %d, expected %d\n", line, argc, expected_argc);
        failures++;
        dfuprog_free_args(&args);
        return;
    }
    for (i = 0; i < expected_argc; i++) {
        if (strcmp(args.argv[i], expected[i]) != 0) {
            printf("FAIL: [%s] -> argv[%d] = [%s], expected [%s]\n", line, i, args.argv[i], expected[i]);
            failures++;
        }
    }
    if (args.argv[argc] != NULL) {
        printf("FAIL: [%s] -> argv is not NULL-terminated\n", line);
        failures++;
    }
    if (args.argc != argc) {
        printf("FAIL: [%s] -> args.argc %d != return value %d\n", line, args.argc, argc);
        failures++;
    }
    dfuprog_free_args(&args);
    if (args.argv != NULL || args.storage != NULL || args.argc != 0) {
        printf("FAIL: [%s] -> dfuprog_free_args did not reset the struct\n", line);
        failures++;
    }
    printf("ok:   [%s] -> %d args\n", line, argc);
}

int main(void)
{
    {
        const char *e[] = { "dfu-programmer", "atxmega32a4u", "erase", "--force", "--debug", "300" };
        check_split("dfu-programmer atxmega32a4u erase --force --debug 300", 6, e);
    }
    {
        /* The bug from #450: the default Windows install directory. */
        const char *e[] = { "dfu-programmer", "atxmega32a4u", "flash",
                            "C:\\Program Files\\EspoTek Labrador\\assets\\firmware\\labrafirm_000C_03.hex",
                            "--debug", "300" };
        check_split("dfu-programmer atxmega32a4u flash \"C:\\Program Files\\EspoTek Labrador\\assets\\firmware\\labrafirm_000C_03.hex\" --debug 300", 6, e);
    }
    {
        /* Quotes opening mid-token, and a Unix path with spaces. */
        const char *e[] = { "dfu-programmer", "atxmega32a4u", "flash", "/Applications/My Apps/labrafirm_000C_03.hex" };
        check_split("dfu-programmer atxmega32a4u flash /Applications/\"My Apps\"/labrafirm_000C_03.hex", 4, e);
    }
    {
        /* Tabs, runs of spaces, leading/trailing whitespace. */
        const char *e[] = { "dfu-programmer", "atxmega32a4u", "launch" };
        check_split("  dfu-programmer\tatxmega32a4u   launch  \n", 3, e);
    }
    {
        /* An empty quoted argument is still an argument. */
        const char *e[] = { "a", "", "b" };
        check_split("a \"\" b", 3, e);
    }
    {
        /* Backslashes are data, never escapes (Windows paths). */
        const char *e[] = { "x", "C:\\a\\b\\" };
        check_split("x \"C:\\a\\b\\\"", 2, e);
    }
    {
        /* Unquoted spaces still split - callers must quote (documented). */
        const char *e[] = { "flash", "C:\\Program", "Files\\x.hex" };
        check_split("flash C:\\Program Files\\x.hex", 3, e);
    }
    {
        const char *e[] = { "" };
        (void)e;
        check_split("", 0, NULL);
        check_split("   \t ", 0, NULL);
    }
    {
        dfuprog_args args;
        if (dfuprog_split_args(NULL, &args) != -1) {
            printf("FAIL: NULL command line must return -1\n");
            failures++;
        } else {
            printf("ok:   NULL command line rejected\n");
        }
        if (dfuprog_split_args("x", NULL) != -1) {
            printf("FAIL: NULL output struct must return -1\n");
            failures++;
        } else {
            printf("ok:   NULL output struct rejected\n");
        }
    }

#ifndef PLATFORM_ANDROID
    {
        /* No board attached: the whole erase path must fail cleanly (non-zero,
         * no crash) and the retry loop must run to completion. */
        int rc;
        dfuprog_set_device_retry(2, 10);
        rc = dfuprog_virtual_cmd("dfu-programmer atxmega32a4u erase --force");
        if (rc == 0) {
            printf("FAIL: erase with no device attached returned success\n");
            failures++;
        } else {
            printf("ok:   erase with no device -> exit code %d\n", rc);
        }
        /* Quoted path: must reach dfu-programmer as one argument and fail on
         * the device, not on argument parsing (ARGUMENT_ERROR == 2 would mean
         * the usage text was printed, i.e. the path was split). */
        rc = dfuprog_virtual_cmd("dfu-programmer atxmega32a4u flash \"/tmp/dir with spaces/labrafirm_000C_03.hex\"");
        if (rc == 0 || rc == 2) {
            printf("FAIL: quoted flash path -> exit code %d (0 = success?!, 2 = argument error)\n", rc);
            failures++;
        } else {
            printf("ok:   quoted flash path accepted, exit code %d (no device)\n", rc);
        }
        dfuprog_set_device_retry(0, 0);
    }
#endif

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all libdfuprog tests passed\n");
    return 0;
}
