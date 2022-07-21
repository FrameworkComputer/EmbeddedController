#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
""" Upload twister results to ResultDB

    Usage:
    $ rdb stream -new -realm chromium:public -- ./util/zephyr_to_resultdb.py
      --results=twister-out/twister.json --upload=True
"""

import argparse
import base64
import json
import os

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

    return ret_status


def translate_expected(status):
    """Translates ZTEST status to ResultDB expected"""
    flag = False

    if status in ["passed", "filtered"]:
        flag = True

    return flag


def testcase_summary(testcase):
    """Translates ZTEST testcase to ResultDB summaryHtml"""
    html = "<p>None</p>"

    if (
        "log" in testcase
        or "reason" in testcase
        or translate_status(testcase["status"]) == "SKIP"
    ):
        html = '<p><text-artifact artifact-id="artifact-content-in-request"></p>'

    return html


def testcase_artifact(testcase):
    """Translates ZTEST testcase to ResultDB artifact"""
    artifact = "Unknown"

    if "log" in testcase:
        artifact = testcase["log"]
    elif "reason" in testcase:
        artifact = testcase["reason"]
    elif testcase["status"] == "filtered":
        artifact = "filtered"

    return base64.b64encode(artifact.encode())


def testcase_to_result(testsuite, testcase):
    """Translates ZTEST testcase to ResultDB format"""
    result = {
        "testId": testcase["identifier"],
        "status": translate_status(testcase["status"]),
        "expected": translate_expected(testcase["status"]),
        "summaryHtml": testcase_summary(testcase),
        "artifacts": {
            "artifact-content-in-request": {
                "contents": testcase_artifact(testcase),
            }
        },
        # TODO(b/239952573) Add all test configs as tags
        "tags": [
            {"key": "category", "value": "ChromeOS/EC"},
            {"key": "platform", "value": testsuite["platform"]},
        ],
        "duration": "%sms" % testcase["execution_time"],
        "testMetadata": {"name": testcase["identifier"]},
    }

    return result


def json_to_resultdb(result_file):
    """Translates Twister json test report to ResultDB format"""
    with open(result_file) as file:
        data = json.load(file)
        results = []

        for testsuite in data["testsuites"]:
            for testcase in testsuite["testcases"]:
                if testcase["status"]:
                    results.append(testcase_to_result(testsuite, testcase))

        file.close()

    return results


class BytesEncoder(json.JSONEncoder):
    """Encoder for ResultDB format"""

    def default(self, obj):
        if isinstance(obj, bytes):
            return obj.decode("utf-8")
        return json.JSONEncoder.default(self, obj)


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
        print("Converting:", args.results)
        rdb_results = json_to_resultdb(args.results)
        if args.upload:
            upload_results(rdb_results)
    else:
        raise Exception("Missing test result file for conversion")


if __name__ == "__main__":
    main()
