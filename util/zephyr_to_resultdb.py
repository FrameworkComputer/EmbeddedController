#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" Upload twister results to ResultDB

    Usage:
    $ rdb stream -new -realm chromium:public -tag builder_name:${HOSTNAME%%.*}
      -- ./util/zephyr_to_resultdb.py --results=twister-out/twister.json
      --upload=True
"""

import argparse
import base64
import datetime
import json
import os
import pathlib
import re

import requests  # pylint: disable=import-error


def translate_status(status):
    """Translates ZTEST status to ResultDB status"""
    ret_status = "SKIP"

    if status == "passed":
        ret_status = "PASS"
    elif status == "failed":
        ret_status = "FAIL"
    elif status in ["skipped", "filtered"]:
        ret_status = "SKIP"
    elif status == "blocked":
        # Twister status for tests that didn't run due to test suite timeout
        ret_status = "ABORT"

    return ret_status


def translate_expected(status):
    """Translates ZTEST status to ResultDB expected"""
    flag = False

    if status in ["passed", "filtered"]:
        flag = True

    return flag


def translate_duration(testcase):
    """Translates ZTEST execution_time to ResultDB duration"""
    time = testcase.get("execution_time")
    if not time:
        return None

    return f"{float(time)/1000:.9f}s"


def testcase_summary(testcase):
    """Translates ZTEST testcase to ResultDB summaryHtml"""
    html = "<p>None</p>"

    if (
        "log" in testcase
        or "reason" in testcase
        or translate_status(testcase["status"]) == "SKIP"
    ):
        html = '<p><text-artifact artifact-id="test_log"></p>'

    return html


def testcase_artifact(testcase):
    """Translates ZTEST testcase to ResultDB artifact"""
    artifact = "Unknown"

    if "log" in testcase and testcase["log"]:
        artifact = testcase["log"]
    elif "reason" in testcase and testcase["reason"]:
        artifact = testcase["reason"]
    elif testcase["status"] == "filtered":
        artifact = "filtered"
    elif testcase["status"] == "skipped":
        artifact = "skipped"

    return base64.b64encode(artifact.encode())


def testsuite_artifact(testsuite):
    """Translates ZTEST testcase to ResultDB artifact"""
    artifact = "Unknown"

    if "log" in testsuite and testsuite["log"]:
        artifact = testsuite["log"]

    return base64.b64encode(artifact.encode())


def testcase_to_result(testsuite, testcase, base_tags, config_tags):
    """Translates ZTEST testcase to ResultDB format
    See TestResult type in
    https://crsrc.org/i/go/src/go.chromium.org/luci/resultdb/sink/proto/v1/test_result.proto
    """
    result = {
        "testId": testcase["identifier"],
        "status": translate_status(testcase["status"]),
        "expected": translate_expected(testcase["status"]),
        "summaryHtml": testcase_summary(testcase),
        "artifacts": {
            "test_log": {
                "contents": testcase_artifact(testcase),
            },
            "testsuite_log": {
                "contents": testsuite_artifact(testsuite),
            },
        },
        "tags": [
            {"key": "suite", "value": testsuite["name"]},
            {"key": "platform", "value": testsuite["platform"]},
        ],
        "duration": translate_duration(testcase),
        "testMetadata": {"name": testcase["identifier"]},
    }

    for key, value in base_tags:
        result["tags"].append({"key": key, "value": value})

    for key, value in config_tags:
        result["tags"].append({"key": key.lower(), "value": value})

    if result["status"] == "FAIL" and "log" in testcase and testcase["log"]:
        assert_msg = re.findall(
            r"Assertion failed.*$", testcase["log"], re.MULTILINE
        )
        if assert_msg:
            result["failureReason"] = {"primaryErrorMessage": assert_msg[0]}
        else:
            result["failureReason"] = {
                "primaryErrorMessage": "Assert not found - possibly occurred in test setup"
            }

    return result


def get_testsuite_config_tags(twister_dir, testsuite):
    """Creates config tags from the testsuite"""
    config_tags = []
    suite_path = f"{twister_dir}/{testsuite['platform']}/{testsuite['name']}"
    dot_config = f"{suite_path}/zephyr/.config"

    if pathlib.Path(dot_config).exists():
        with open(dot_config) as file:
            lines = file.readlines()

            for line in lines:
                # Ignore empty lines and comments
                if line.strip() and not line.startswith("#"):
                    result = re.search(r"(\w+)=(.+$)", line)
                    config_tags.append((result.group(1), result.group(2)))
    else:
        print(f"Can't find config file for {testsuite['name']}")

    return config_tags


def create_base_tags(data):
    """Creates base tags needed for Testhaus"""
    base_tags = []

    queued_time = datetime.datetime.fromisoformat(
        data["environment"]["run_date"]
    )
    base_tags.append(
        ("queued_time", queued_time.strftime("%Y-%m-%d %H:%M:%S.%f UTC"))
    )

    base_tags.append(("zephyr_version", data["environment"]["zephyr_version"]))
    base_tags.append(("board", data["environment"]["os"]))
    base_tags.append(("toolchain", data["environment"]["toolchain"]))

    return base_tags


def json_to_resultdb(result_file):
    """Translates Twister json test report to ResultDB format"""
    with open(result_file) as file:
        data = json.load(file)
        results = []
        base_tags = create_base_tags(data)

        for testsuite in data["testsuites"]:
            config_tags = get_testsuite_config_tags(
                os.path.dirname(result_file), testsuite
            )
            for testcase in testsuite["testcases"]:
                if testcase["status"]:
                    results.append(
                        testcase_to_result(
                            testsuite, testcase, base_tags, config_tags
                        )
                    )

        file.close()

    return results


class BytesEncoder(json.JSONEncoder):
    """Encoder for ResultDB format"""

    def default(self, o):
        if isinstance(o, bytes):
            return o.decode("utf-8")
        return json.JSONEncoder.default(self, o)


def upload_results(results):
    """Upload results to ResultDB"""
    with open(os.environ["LUCI_CONTEXT"]) as file:
        sink = json.load(file)["result_sink"]

    # Uploads all test results at once.
    res = requests.post(
        url="http://%s/prpc/luci.resultsink.v1.Sink/ReportTestResults"
        % sink["address"],
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json",
            "Authorization": "ResultSink %s" % sink["auth_token"],
        },
        data=json.dumps({"testResults": results}, cls=BytesEncoder),
    )
    res.raise_for_status()


def main():
    """main"""
    # Set up argument parser.
    parser = argparse.ArgumentParser(
        description=("Upload Zephyr Twister test results to ResultDB")
    )
    parser.add_argument("--results")
    parser.add_argument("--upload", default=False)
    args = parser.parse_args()

    if args.results:
        print(f"Converting: {args.results}")
        rdb_results = json_to_resultdb(args.results)
        if args.upload:
            upload_results(rdb_results)
    else:
        raise Exception("Missing test result file for conversion")


if __name__ == "__main__":
    main()
