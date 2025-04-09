// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io/ioutil"
	"log"
	"os"
	"path/filepath"

	"google.golang.org/protobuf/proto"

	"github.com/bmatcuk/doublestar"
	"go.chromium.org/chromiumos/infra/proto/go/chromiumos"
)

const builderConfigsPath = "../../../../../infra/config/generated/builder_configs.binaryproto"
const cqConfigName = "betty-cq"
const ecDir = "../.."
const ecPrefix = "src/platform/ec/"

var expectedCQPatterns = []string{
	// chromeos-base/chromeos-ec-headers:
	"src/platform/ec/include/{ec_commands,ec_cmd_api,panic_defs,cros_ec_dev}.h",
	// chromeos-base/zephyr-build-tools
	"src/platform/ec/zephyr/zmake/**",
	// chromeos-base/ec-utils/ec-utils (Optional, uses pupr): make BOARD=host utils-host && cat build/host/util/*.d | tr ' ' '\012' | sort -u
	// chromeos-base/ec-utils/ec-devutils (Optional, uses pupr):
	//   include/compile_time_macros.h util/misc_util.h include/update_fw.h include/usb_descriptor.h include/vb21_struct.h include/2id.h extra/usb_updater/usb_updater2.c
	//   extra/touchpad_updater/touchpad_updater.c
	//   util/flash_ec util/uart_stress_tester.py util/openocd/*
	"src/platform/ec/extra/touchpad_updater/**",
	// chromeos-base/ec-utils-test (Optional, uses pupr): util/battery_temp util/inject-keys.py util/flash_fp_mcu util/fptool.py
	// extra/rma_reset/* common/curve25519.c common/curve25519-generic.c common/sha256.c common/base32.c
	// include/base32.h include/common.h include/util.h include/sha256.h builtin/assert.h
	"src/platform/ec/extra/rma_reset/**",
	// TODO: Switch to only the actual deps instead of all of the util dir
	"src/platform/ec/util/**",
	// BUILD.py changes that don't match src/project/*/*/config.star can break.
	"src/platform/ec/zephyr/program/**/BUILD.py",
}

// gitIgnores are patterns that would normally be ignored by git.
var gitIgnores = []string{
	"src/platform/ec/**/__pycache__/**",
	"src/platform/ec/zephyr/zmake/.hypothesis/**",
}

func main() {
	var excludePatterns []string

	// If no patterns were given on command line, then check the
	if len(os.Args) <= 1 {
		data, err := ioutil.ReadFile(builderConfigsPath)
		if err != nil {
			log.Fatal("Error reading file:", err)
			return
		}

		builderConfigs := &chromiumos.BuilderConfigs{}
		err = proto.Unmarshal(data, builderConfigs)
		if err != nil {
			log.Fatal("Error unmarshaling data:", err)
			return
		}

		// Find the cq config
		for _, config := range builderConfigs.GetBuilderConfigs() {
			if cqConfigName == config.GetId().GetName() {
				if config.GetGeneral().GetRunWhen().GetMode() != chromiumos.BuilderConfig_General_RunWhen_NO_RUN_ON_FILE_MATCH {
					log.Fatalf("Unexpected general.run_when.mode on %q. got %v, want %v", cqConfigName, config.GetGeneral().GetRunWhen().GetMode(), chromiumos.BuilderConfig_General_RunWhen_NO_RUN_ON_FILE_MATCH)
					return
				}
				excludePatterns = config.GetGeneral().GetRunWhen().GetFilePatterns()
			}
		}
		if excludePatterns == nil {
			log.Fatalf("Did not find cq config for %q in %q", cqConfigName, builderConfigsPath)
			return
		}
	} else {
		excludePatterns = os.Args[1:]
	}

	numErrors := 0
	err := filepath.WalkDir(ecDir, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		relPath, err := filepath.Rel(ecDir, path)
		if err != nil {
			return err
		}
		absPath := filepath.Join(ecPrefix, relPath)

		// Exit the function for ignored files.
		for _, pattern := range gitIgnores {
			ok, err := doublestar.Match(pattern, absPath)
			if err != nil {
				return err
			}
			if ok {
				return nil
			}
		}

		// Find files that should be run through CQ.
		wantExclude := true
		for _, pattern := range expectedCQPatterns {
			// log.Printf("Checking %q in expected %q", absPath, pattern)
			ok, err := doublestar.Match(pattern, absPath)
			if err != nil {
				return err
			}
			if ok {
				// log.Printf("%q matched %q", absPath, pattern)
				wantExclude = false
				break
			}
		}

		// Check if they are excluded from the CQ.
		excluded := false
		for _, pattern := range excludePatterns {
			// log.Printf("Checking %q in exclude %q", absPath, pattern)
			ok, err := doublestar.Match(pattern, absPath)
			if err != nil {
				return err
			}
			if ok {
				// log.Printf("%q matched %q", absPath, pattern)
				excluded = true
				break
			}
		}
		if excluded && !wantExclude {
			log.Printf("%q was excluded from CQ, but should have been included", absPath)
			numErrors++
		}
		if !excluded && wantExclude {
			log.Printf("%q was included in CQ, but should have been excluded", absPath)
			numErrors++
		}
		return nil
	})
	if err != nil {
		log.Fatal("Error getting files: ", err)
		return
	}
	if numErrors > 0 {
		log.Printf("Edit infra/config/build_targets.star, and run cd infra/config; ./regenerate_configs.py -b")
		log.Fatalf("%d files were incorrectly filtered", numErrors)
		return
	}
}
