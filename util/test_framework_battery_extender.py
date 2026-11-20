#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Host regression for Framework's charge-limit command.

Compile the complete production translation unit and real protocol headers.
Mock only its EC services. No hardware or firmware flashing is involved.
Run with: python3 util/test_framework_battery_extender.py
Requires Python 3.8+ and a C compiler (cc, or set CC). Coverage needs GCC/gcov.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "zephyr/program/framework/src/battery_extender.c"
STUB_HEADERS = (
    "battery.h battery_smart.h board_charger.h battery_fuel_gauge.h "
    "board_function.h charge_state.h charger.h console.h "
    "customized_shared_memory.h extpower.h factory.h host_command.h "
    "system.h timer.h util.h hooks.h zephyr/sys/time_units.h"
).split()

HARNESS = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ec_commands.h"
#include "board_host_command.h"

#define USEC_PER_SEC 1000000U
#define SEC_PER_MIN 60U
#define SEC_PER_HOUR 3600U
/* Match legacy timer.h when running this regression against the baseline. */
#define SECOND 1000000
#define MINUTE 60000000
#define HOUR 3600000000ULL
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define EC_SUCCESS 0
#define EC_ERROR_PARAM1 1
#define EC_ERROR_PARAM2 2
#define EC_ERROR_PARAM3 3
#define EC_ERROR_PARAM_COUNT 4
#define SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX 0
#define DECLARE_HOOK(...)
#define DECLARE_HOST_COMMAND(...)
#define DECLARE_CONSOLE_COMMAND(...)
#define cprints(...) ((void)0)
#define cprintf(...) ((void)0)

typedef union { uint64_t val; } timestamp_t;
struct host_cmd_handler_args {
    const void *params;
    void *response;
    int response_size;
    int version;
};
static uint8_t saved_limit = 80;
static int lower = -1, upper = -1, mode;
static uint64_t now;
static int ac_present = 1;
static timestamp_t get_time(void) { return (timestamp_t){ .val = now }; }
static bool timestamp_expired(timestamp_t deadline, const timestamp_t *time)
{ return deadline.val <= time->val; }
static int system_get_bbram(int idx, uint8_t *value)
{ *value = saved_limit; return 0; }
static int system_set_bbram(int idx, uint8_t value)
{ saved_limit = value; return 0; }
static bool battery_sustainer_enabled(void)
{ return lower != -1 && upper != -1; }
static int battery_sustainer_set(int8_t lo, int8_t hi)
{ lower = lo; upper = hi; return 0; }
static int set_chg_ctrl_mode(enum ec_charge_control_mode value)
{ mode = value; return 0; }
static int factory_status(void) { return 0; }
static int extpower_is_present(void) { return ac_present; }
static int strtoi(const char *s, char **e, int base)
{ return strtol(s, e, base); }
static int parse_bool(const char *s, int *value)
{ *value = !strcmp(s, "enable"); return 1; }

#include "PRODUCTION_SOURCE"

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "%s\n", message); return 1; } \
} while (0)

static enum ec_status send(uint8_t operation, uint8_t limit)
{
    struct ec_params_ec_chg_limit_control p = {
        .modes = operation, .max_percentage = limit
    };
    struct ec_response_chg_limit_control r = {0};
    struct host_cmd_handler_args args = { .params = &p, .response = &r };
    return cmd_charging_limit_control(&args);
}

int main(int argc, char **argv)
{
    extender_init();
    batt_extender_disable = true;
    CHECK(send(CHG_LIMIT_SET_LIMIT, 80) == EC_RES_SUCCESS, "initial SET failed");
    CHECK(lower == 75 && upper == 80, "initial SET did not apply 75..80");
    if (!strcmp(argv[1], "disabled")) {
        battery_sustainer_set(-1, -1);
        send(CHG_LIMIT_SET_LIMIT, 80);
        CHECK(lower == 75 && upper == 80,
              "same-limit SET left the sustainer disabled");
    } else if (!strcmp(argv[1], "replaced")) {
        battery_sustainer_set(90, 95);
        send(CHG_LIMIT_SET_LIMIT, 80);
        CHECK(lower == 75 && upper == 80,
              "same-limit SET kept another policy's thresholds");
    } else if (!strcmp(argv[1], "host_override")) {
        battery_sustainer_set(-1, -1);
        battery_extender();
        send(CHG_LIMIT_GET_LIMIT, 0);
        CHECK(!battery_sustainer_enabled(),
              "timer or GET cancelled intentional host control");
    } else if (!strncmp(argv[1], "stage", 5)) {
        send(CHG_LIMIT_SET_LIMIT, 98);
        batt_extender_disable = false;
        stage = !strcmp(argv[1], "stage1") ?
            BATT_EXTENDER_STAGE_1 : BATT_EXTENDER_STAGE_2;
        int cap_lower = stage == BATT_EXTENDER_STAGE_1 ? 90 : 85;
        int cap_upper = stage == BATT_EXTENDER_STAGE_1 ? 95 : 87;
        sustainer_lower = 93;
        sustainer_upper = 98;
        batt_extender_deadline.val = 0;
        batt_extender_deadline_stage2.val = 0;
        battery_sustainer_set(cap_lower, cap_upper);
        send(CHG_LIMIT_SET_LIMIT, 98);
        CHECK(lower == cap_lower && upper == cap_upper,
              "same-limit SET removed the active extender cap");
        battery_extender();
        CHECK(lower == cap_lower && upper == cap_upper,
              "timer removed the active extender cap");
    } else if (!strcmp(argv[1], "full_and_invalid")) {
        CHECK(send(CHG_LIMIT_SET_LIMIT, 19) == EC_RES_ERROR,
              "invalid SET was accepted");
        CHECK(lower == 75 && upper == 80, "invalid SET changed thresholds");
        send(CHG_LIMIT_SET_LIMIT, 100);
        CHECK(!battery_sustainer_enabled(), "100 percent did not disable limiting");
        send(CHG_LIMIT_SET_LIMIT, 80);
        CHECK(lower == 75 && upper == 80, "changed-limit SET failed");
    } else if (!strcmp(argv[1], "extender_timing")) {
        send(CHG_LIMIT_SET_LIMIT, 100);
        batt_extender_disable = false;
        batt_extender_deadline.val = 1000000;
        batt_extender_deadline_stage2.val = 2000000;
        now = 999999;
        battery_extender();
        CHECK(stage == BATT_EXTENDER_STAGE_0, "stage one began early");
        now = 1000000;
        battery_extender();
        CHECK(stage == BATT_EXTENDER_STAGE_1 && lower == 90 && upper == 95,
              "stage one missed its deadline or cap");
        now = 2000000;
        battery_extender();
        CHECK(stage == BATT_EXTENDER_STAGE_2 && lower == 85 && upper == 87,
              "stage two missed its deadline or cap");
        ac_present = 0;
        now = reset_deadline.val;
        battery_extender();
        CHECK(stage == BATT_EXTENDER_STAGE_0 && !battery_sustainer_enabled(),
              "AC absence did not reset the extender after its interval");
    } else if (!strcmp(argv[1], "configuration")) {
        struct ec_params_battery_extender p = {
            .cmd = BATT_EXTENDER_WRITE_CMD, .trigger_days = 99,
            .reset_minutes = 9999, .disable = 0
        };
        struct ec_response_battery_extender r = {0};
        struct host_cmd_handler_args args = { .params = &p, .response = &r };
        CHECK(battery_extender_hc(&args) == EC_SUCCESS, "configuration failed");
        CHECK(battery_extender_trigger == 8553600000000ULL,
              "99-day trigger was truncated");
        CHECK(battery_extender_reset == 599940000000ULL,
              "9999-minute reset was truncated");
        p.cmd = BATT_EXTENDER_READ_CMD;
        CHECK(battery_extender_hc(&args) == EC_SUCCESS, "readback failed");
        CHECK(r.trigger_days == 99 && r.reset_minutes == 9999,
              "configuration did not round-trip");
    } else if (!strcmp(argv[1], "console_timing")) {
        const char *args[] = { "batt_extender", "trigger", "2", "s" };
        CHECK(cmd_parse_timestamp(4, args) == 2000000ULL,
              "seconds changed during time-unit migration");
        args[3] = "m";
        CHECK(cmd_parse_timestamp(4, args) == 120000000ULL, "minutes changed");
        args[3] = "h";
        CHECK(cmd_parse_timestamp(4, args) == 7200000000ULL, "hours changed");
        args[3] = "d";
        CHECK(cmd_batt_extender(4, args) == EC_SUCCESS, "console trigger failed");
        CHECK(battery_extender_trigger == 172800000000ULL, "days changed");
        CHECK(cmd_batt_extender(1, args) == EC_SUCCESS, "console status failed");
    } else if (!strcmp(argv[1], "console_controls")) {
        const char *args[] = { "batt_extender", "enable", "2", "m" };
        now = 123;
        CHECK(cmd_batt_extender(2, args) == EC_SUCCESS,
              "console enable failed");
        CHECK(!batt_extender_disable && batt_extender_deadline.val > now,
              "enable did not arm the extender");
        args[1] = "timeext";
        CHECK(cmd_batt_extender(4, args) == EC_SUCCESS, "stage-one timer failed");
        CHECK(batt_extender_deadline.val == 120000123ULL,
              "stage-one timer used the wrong unit");
        args[1] = "timeext2";
        CHECK(cmd_batt_extender(4, args) == EC_SUCCESS, "stage-two timer failed");
        CHECK(batt_extender_deadline_stage2.val == 120000123ULL,
              "stage-two timer used the wrong unit");
        args[1] = "timerst";
        CHECK(cmd_batt_extender(4, args) == EC_SUCCESS, "reset timer failed");
        CHECK(reset_deadline.val == 120000123ULL,
              "reset timer used the wrong unit");
        args[1] = "reset";
        args[3] = "h";
        CHECK(cmd_batt_extender(4, args) == EC_SUCCESS, "reset setting failed");
        CHECK(battery_extender_reset == 7200000000ULL,
              "reset setting used the wrong unit");
        args[1] = "disable";
        CHECK(cmd_batt_extender(2, args) == EC_SUCCESS, "disable failed");
        CHECK(batt_extender_disable && stage == BATT_EXTENDER_STAGE_0,
              "disable did not return to stage zero");
    } else {
        return 2;
    }
    return 0;
}
"""


class ChargeLimitTest(unittest.TestCase):
    """Exercise explicit SET and the policies it must preserve."""

    coverage_dir = None

    @classmethod
    def setUpClass(cls):
        # unittest owns and closes this directory after the class finishes.
        # pylint: disable=consider-using-with
        temporary = tempfile.TemporaryDirectory()
        # pylint: enable=consider-using-with
        cls.addClassCleanup(temporary.cleanup)
        path = Path(temporary.name)
        for header in STUB_HEADERS:
            (path / header).parent.mkdir(parents=True, exist_ok=True)
            (path / header).write_text(
                "/* EC service mocked by the host test. */\n"
            )
        c = path / "regression.c"
        c.write_text(HARNESS.replace("PRODUCTION_SOURCE", SOURCE.as_posix()))
        cls.binary = path / "regression"
        coverage_flags = ["--coverage"] if cls.coverage_dir else []
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-std=gnu11",
                "-Wall",
                "-Werror",
                *coverage_flags,
                "-Wno-unused-function",
                "-Wno-unused-variable",
                "-I",
                str(path),
                "-I",
                str(ROOT / "zephyr/program/framework/include"),
                "-I",
                str(ROOT / "include"),
                str(c),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )
        if cls.coverage_dir:
            cls.coverage_dir.mkdir(parents=True, exist_ok=True)
            cls.addClassCleanup(cls.export_coverage, c)

    @classmethod
    def export_coverage(cls, source):
        """Save GCC's coverage data before the temporary files are removed."""
        subprocess.run(
            ["gcov", "--json-format", "--branch-probabilities", str(source)],
            cwd=cls.coverage_dir,
            check=True,
        )

    def check_case(self, case):
        """Run an isolated production-code scenario and expose its assertion."""
        result = subprocess.run(
            [self.binary, case], capture_output=True, text=True, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_same_limit_restores_disabled_sustainer(self):
        """An explicit repeated SET must restore disabled static thresholds."""
        self.check_case("disabled")

    def test_same_limit_restores_replaced_thresholds(self):
        """A repeated static SET must replace another policy's thresholds."""
        self.check_case("replaced")

    def test_timer_and_get_preserve_host_override(self):
        """Readback and timer activity must not cancel intentional host control."""
        self.check_case("host_override")

    def test_same_limit_preserves_stage_one_cap(self):
        """A repeated SET must preserve the first active extender cap."""
        self.check_case("stage1")

    def test_same_limit_preserves_stage_two_cap(self):
        """A repeated SET must preserve the second active extender cap."""
        self.check_case("stage2")

    def test_full_charge_and_invalid_limit(self):
        """Reject invalid limits and retain full-charge/static transitions."""
        self.check_case("full_and_invalid")

    def test_extender_deadlines_and_ac_reset(self):
        """Keep both cap deadlines and the AC-absence reset interval."""
        self.check_case("extender_timing")

    def test_configuration_round_trips_maximum_intervals(self):
        """Keep valid multi-day/minute intervals wider than 32 bits."""
        self.check_case("configuration")

    def test_console_time_units(self):
        """Keep seconds, minutes, hours and days in microseconds."""
        self.check_case("console_timing")

    def test_console_enable_disable_and_timers(self):
        """Keep public console controls and their configured intervals."""
        self.check_case("console_controls")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--coverage-dir", type=Path)
    options, test_args = parser.parse_known_args()
    ChargeLimitTest.coverage_dir = options.coverage_dir
    unittest.main(argv=[sys.argv[0], *test_args], verbosity=2)
