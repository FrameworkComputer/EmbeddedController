/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

extern "C" {
#include "include/pd_driver.h"
#include "include/platform.h"
#include "ppm_common.h"
#include "smbus_usermode.h"
#include "um_ppm_chardev.h"
}

using testing::_;
using testing::Invoke;
using std::string;
using std::vector;
using std::map;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

class PdDriverMock {
    public:
	virtual ~PdDriverMock()
	{
	}

	/* Mocked method(s) */
	MOCK_METHOD(int, execute_cmd,
		    (struct ucsi_pd_device *, struct ucsi_control *,
		     uint8_t *));
};

class TestFixture : public ::testing::Test {
    public:
	TestFixture()
	{
		pd_driver_mock_.reset(new ::testing::NiceMock<PdDriverMock>());
	}

	~TestFixture()
	{
		pd_driver_mock_.reset();
	}

	/* Pointer for accessing mocked PD driver */
	static std::unique_ptr<PdDriverMock> pd_driver_mock_;
};

/* Instantiate mocked PD driver */
std::unique_ptr<PdDriverMock> TestFixture::pd_driver_mock_;

/*
 * Function execute_cmd() is the center point for mocking PD driver.
 * In normal circumstance the execute_cmd() communicates with the PD
 * chip to perform a command it received. For unit test purposes the
 * execute_cmd() in our case is a C wrapper around C++ mocked
 * execute_cmd(). This allow us to handle OPM/PPM initiated requests
 * which use UCSI protocol in the mocked execute_cmd and verify
 * compliance of the OPM/PPM to the UCSI specification.
 *
 * During each test setup the new PdDriverMock is created and
 * assigned to the pd_driver_mock_ unique_ptr by calling reset
 * method.
 */
int execute_cmd(struct ucsi_pd_device *dev, struct ucsi_control *ctrl,
		uint8_t *lpm_data_out)
{
	return TestFixture::pd_driver_mock_->execute_cmd(dev, ctrl,
							 lpm_data_out);
}

class Cmd {
    public:
	Cmd(string name, vector<uint8_t> ref_cmd)
	{
		name_ = name;
		ref_cmd_ = ref_cmd;
	}

	Cmd(string name, vector<uint8_t> ref_cmd, vector<uint8_t> ref_resp)
	{
		name_ = name;
		ref_cmd_ = ref_cmd;
		ref_resp_ = ref_resp;
	}

	Cmd(string name, vector<uint8_t> ref_cmd, int ref_resp_val)
	{
		name_ = name;
		ref_cmd_ = ref_cmd;
		ref_resp_val_ = ref_resp_val;
	}

	typedef int (Cmd::*ExecuteCmdPtr)(struct ucsi_pd_device *,
					  struct ucsi_control *, uint8_t *);

	int ExecuteCmd(struct ucsi_pd_device *dev, struct ucsi_control *ctrl,
		       uint8_t *lpm_data_out)
	{
		uint8_t ref_cmd_bytes[kUcsiCmdLen] = { 0 };
		size_t rlen = ref_resp_.size();
		size_t clen = ref_cmd_.size();
		bool cmp;

		PrintCmdDbg(ctrl, ++iter_cnt_);
		memcpy(ref_cmd_bytes, ref_cmd_.data(), clen);
		cmp = !memcmp(ctrl, ref_cmd_bytes, kUcsiCmdLen);

		if (!cmp) {
			DLOG_START("REFERENCE COMMAND: size 0x%x, data", clen);
			for (uint8_t val : ref_cmd_)
				DLOG_LOOP(" 0x%x", val);
			DLOG_END("\n");
		}

		EXPECT_TRUE(cmp)
			<< "RCV COMMAND DOESN'T MATCH REF " << name_ << " CMD";

		if (rlen > 0)
			memcpy(lpm_data_out, ref_resp_.data(), rlen);

		return ref_resp_val_ ? ref_resp_val_ : rlen;
	}

	static int ErrorNoHandlerForCmd(struct ucsi_pd_device *dev,
					struct ucsi_control *ctrl,
					uint8_t *lpm_data_out)
	{
		PrintCmdDbg(ctrl, 0);
		EXPECT_EQ(0, 1) << "ERROR NO HANDLER FOR COMMAND: "
				<< (int)ctrl->command;
		return 0;
	}

	static void PrintCmdDbg(struct ucsi_control *ctrl, int iter_cnt)
	{
		DLOG("RECEIVED COMMAND ITER(%d): command 0x%x, data_length 0x%x,"
		     "command_specific 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		     iter_cnt, ctrl->command, ctrl->data_length,
		     ctrl->command_specific[0], ctrl->command_specific[1],
		     ctrl->command_specific[2], ctrl->command_specific[3],
		     ctrl->command_specific[4], ctrl->command_specific[5]);
	}

	static void ResetCounter()
	{
		iter_cnt_ = 0;
	}

    protected:
	string name_;
	int ref_resp_val_;
	vector<uint8_t> ref_cmd_;
	vector<uint8_t> ref_resp_;
	static std::atomic<int> iter_cnt_;
	static const size_t kUcsiCmdLen = 8;
};

std::atomic<int> Cmd::iter_cnt_{ 0 };

class SysfsVerifier {
    public:
	SysfsVerifier(int num_ports)
		: num_ports_(num_ports)
	{
	}

	void AddDirToCheck(string &dir, bool exist = true)
	{
		location_.push_back(make_tuple(dir, exist, ""));
	}

	void AddFileToCheck(string &file, string &value, bool exist = true)
	{
		location_.push_back(make_tuple(file, exist, value));
	}

	int Verify(int timeout_ms)
	{
		for (auto tuple : location_) {
			auto &[loc, exist, val] = tuple;
			auto tmo = timeout_ms;
			bool file_exist;

			while (tmo) {
				std::ifstream file(loc);

				file_exist = file.good();
				if (file_exist == exist) {
					if (!val.length())
						break;

					string rval;
					getline(file, rval);
					if (!val.compare(rval))
						break;

					ELOG("Value '%s' in file '%s' does not match expected value '%s'",
					     rval.c_str(), loc.c_str(),
					     val.c_str());
					return -EINVAL;
				}

				sleep_for(milliseconds(kSleepInMs));
				tmo -= kSleepInMs;
			}

			if (tmo <= 0) {
				ELOG("Failed to verify %s '%s' %s",
				     !val.length() ? "directory" : "file",
				     loc.c_str(),
				     exist ? "does exist" : "does not exist");
				return -ETIMEDOUT;
			}

			if (!val.length() || !exist) {
				DLOG("Verified '%s' %s %s", loc.c_str(),
				     !val.length() ? "directory" : "file",
				     exist ? "does exist" : "does not exist");
			} else
				DLOG("Verified file '%s' with value '%s' exists",
				     loc.c_str(), val.c_str());
		}

		return 0;
	}

	int Verify()
	{
		return Verify(kTmoInMs);
	};

	void Clear()
	{
		location_.clear();
		ports_map_.clear();
	}

	void VerifyPortProperties(int port_num)
	{
		DetectPorts();
		if (!ValidatePortNumber(port_num))
			return;
		location_.clear();

		string num = std::to_string(ports_map_[port_num]);

		string port_dir("/sys/class/typec/port" + num);
		AddDirToCheck(port_dir);

		string am0_svid_file(port_dir + "/port" + num + ".0/svid");
		string am0_svid_value("8087");
		AddFileToCheck(am0_svid_file, am0_svid_value);

		string am1_svid_file(port_dir + "/port" + num + ".1/svid");
		string am1_svid_value("17ef");
		AddFileToCheck(am1_svid_file, am1_svid_value);

		string am2_svid_file(port_dir + "/port" + num + ".2/svid");
		string am2_svid_value("ff01");
		AddFileToCheck(am2_svid_file, am2_svid_value);

		string pd_rev_file(port_dir + "/usb_power_delivery_revision");
		string pd_rev_value("3.0");
		AddFileToCheck(pd_rev_file, pd_rev_value);

		string typec_rev_file(port_dir + "/usb_typec_revision");
		string typec_rev_value("1.3");
		AddFileToCheck(typec_rev_file, typec_rev_value);

		string port_partner_dir("/sys/class/typec/port" + num +
					"-partner");
		AddDirToCheck(port_partner_dir, false);

		EXPECT_EQ(0, Verify())
			<< "FAILED TO VERIFY PORT" << port_num << " PROPERTIES";
	}

	void VerifyPortPartnerProperties(int port_num)
	{
		DetectPorts();
		if (!ValidatePortNumber(port_num))
			return;
		location_.clear();

		string num = std::to_string(ports_map_[port_num]);

		string port_dir("/sys/class/typec/port" + num + "-partner");
		AddDirToCheck(port_dir);

		string am_desc_file(port_dir + "/port" + num +
				    "-partner.0/description");
		string am_desc_value("DisplayPort");
		AddFileToCheck(am_desc_file, am_desc_value);

		string am_svid_file(port_dir + "/port" + num +
				    "-partner.0/svid");
		string am_svid_value("ff01");
		AddFileToCheck(am_svid_file, am_svid_value);

		string am_num_file(port_dir + "/number_of_alternate_modes");
		string am_num_value("1");
		AddFileToCheck(am_num_file, am_num_value);

		string pd_rev_file(port_dir + "/usb_power_delivery_revision");
		string pd_rev_value("3.0");
		AddFileToCheck(pd_rev_file, pd_rev_value);

		EXPECT_EQ(0, Verify()) << "FAILED TO VERIFY PORT" << num
				       << "-PARTNER PROPERTIES";
	}

	void VerifyPortCableProperties(int port_num)
	{
		DetectPorts();
		if (!ValidatePortNumber(port_num))
			return;
		location_.clear();

		string num = std::to_string(ports_map_[port_num]);

		string cable_dir("/sys/class/typec/port" + num + "-cable");
		AddDirToCheck(cable_dir);

		string plug_dir("/sys/class/typec/port" + num + "-plug0");
		AddDirToCheck(plug_dir);

		string id_cert_file(cable_dir + "/identity/cert_stat");
		string id_cert_value("0xb0690712");
		AddFileToCheck(id_cert_file, id_cert_value);

		string hdr_file(cable_dir + "/identity/id_header");
		string hdr_value("0x00000000");
		AddFileToCheck(hdr_file, hdr_value);

		string prod_file(cable_dir + "/identity/product");
		string prod_value("0x00000001");
		AddFileToCheck(prod_file, prod_value);

		string vdo1_file(cable_dir + "/identity/product_type_vdo1");
		string vdo1_value("0x00000000");
		AddFileToCheck(vdo1_file, vdo1_value);

		string vdo2_file(cable_dir + "/identity/product_type_vdo2");
		string vdo2_value("0x00000000");
		AddFileToCheck(vdo2_file, vdo2_value);

		string vdo3_file(cable_dir + "/identity/product_type_vdo3");
		string vdo3_value("0x00000000");
		AddFileToCheck(vdo3_file, vdo3_value);

		string plug_file(cable_dir + "/plug_type");
		string plug_value("type-c");
		AddFileToCheck(plug_file, plug_value);

		string pd_rev_file(cable_dir + "/usb_power_delivery_revision");
		string pd_rev_value("3.0");
		AddFileToCheck(pd_rev_file, pd_rev_value);

		EXPECT_EQ(0, Verify()) << "FAILED TO VERIFY PORT" << num
				       << "-CABLE PROPERTIES";
	}

	void VerifyPortPartnerExistence(int port_num, bool port_exists = false)
	{
		DetectPorts();
		if (!ValidatePortNumber(port_num))
			return;
		location_.clear();

		string num = std::to_string(ports_map_[port_num]);
		string port_dir("/sys/class/typec/port" + num + "-partner");
		AddDirToCheck(port_dir, port_exists);
		EXPECT_EQ(0, Verify())
			<< "PORT" << num << "-PARTNER DIR STILL EXISTS ";
	}

	void VerifyPortCableExistence(int port_num, bool port_exists = false)
	{
		DetectPorts();
		if (!ValidatePortNumber(port_num))
			return;
		location_.clear();

		string num = std::to_string(ports_map_[port_num]);

		string cable_dir("/sys/class/typec/port" + num + "-cable");
		AddDirToCheck(cable_dir, port_exists);

		string plug_dir("/sys/class/typec/port" + num + "-plug0");
		AddDirToCheck(plug_dir, port_exists);

		EXPECT_EQ(0, Verify())
			<< "PORT" << num << "-CABLE DIR STILL EXISTS ";
	}

    protected:
	bool ValidatePortNumber(int port_num)
	{
		if (ports_map_.find(port_num) == ports_map_.end()) {
			EXPECT_EQ(0, 1) << "INVALID PORT NUMBER " << port_num;
			return false;
		}

		return true;
	}

	void DetectPorts()
	{
		string base("/sys/class/typec/");
		int tmo = kTmoInMs;
		int num_ports = 1;
		int index = 0;

		if (ports_detected_)
			return;

		do {
			std::filesystem::path path(base + "port" +
						   std::to_string(index));

			if (!std::filesystem::exists(path)) {
				if (num_ports_ == num_ports - 1) {
					ports_detected_ = true;
					break;
				}

				sleep_for(milliseconds(kSleepInMs));
				tmo -= kSleepInMs;
				if (tmo <= 0)
					break;

				ports_map_.clear();
				num_ports = 1;
				index = 0;
				continue;
			}

			if (!std::filesystem::is_symlink(path))
				continue;

			auto link = std::filesystem::read_symlink(path);
			for (auto p : link) {
				if (p.string().find("ucsi_um_test_device") ==
				    std::string::npos)
					continue;

				/* Found ucsi_um_test_device port */
				ports_map_[num_ports++] = index;
			}
			index++;

		} while (tmo);

		EXPECT_EQ(num_ports_, num_ports - 1)
			<< "DETECTED NUM OF PORTS(" << num_ports - 1
			<< ") DIFFERS FROM EXPECTED NUM OF PORTS(" << num_ports_
			<< ")";

		if (ports_map_.size()) {
			DLOG("PORTS MAPPING");
			for (auto [key, val] : ports_map_)
				DLOG("%d -> %d", key, val);
		}
	}

	const int kTmoInMs = 5000;
	const int kSleepInMs = 250;
	vector<std::tuple<string, bool, string> > location_;
	map<int, int> ports_map_;
	bool ports_detected_;
	int num_ports_;
};

class OpmUnitTest : public TestFixture {
    public:
	OpmUnitTest()
		: num_ports_(2)
	{
		/* Initialize ucsi_pd_driver */
		pd_drv_.dev = reinterpret_cast<struct ucsi_pd_device *>(this);
		pd_drv_.configure_lpm_irq = ConfigureLpmIrq;
		pd_drv_.init_ppm = InitPpm;
		pd_drv_.get_ppm = GetPpm;
		pd_drv_.execute_cmd = execute_cmd;
		pd_drv_.get_active_port_count = PdGetActivePortCount;
		pd_drv_.cleanup = PdCleanup;

		/* Initialize smbus_drv */
		smbus_drv_.dev = reinterpret_cast<struct smbus_device *>(this);
		smbus_drv_.block_for_interrupt = BlockForInterrupt;
		smbus_drv_.cleanup = SmbusCleanup;

		/* Reset counter */
		Cmd::ResetCounter();
	}

	virtual ~OpmUnitTest()
	{
	}

	void SetUp()
	{
		ppm_drv_ = ppm_open(&pd_drv_);
		if (!ppm_drv_)
			FAIL() << "Initializing ppm driver failed.";
	}

	void TearDown()
	{
		/* Clean up main loop (cdev) */
		pthread_kill(thread_, SIGTERM);
		pthread_join(thread_, NULL);

		/* Clean up ppm */
		ppm_drv_->cleanup(ppm_drv_);
	}

	static void *MainLoop(void *ptr)
	{
		OpmUnitTest *p = reinterpret_cast<OpmUnitTest *>(ptr);
		string sdev("/dev/ucsi_um_test-0");

		cdev_prepare_um_ppm(sdev.c_str(), p->GetPdDrv(),
				    p->GetSmbusDrv(), p->GetConfig());
		return NULL;
	}

	static int PdGetActivePortCount(struct ucsi_pd_device *dev)
	{
		return PpmCast(dev)->GetActivePortCount();
	}

	static struct ucsi_ppm_driver *GetPpm(struct ucsi_pd_device *dev)
	{
		return PpmCast(dev)->GetPpmDrv();
	}

	static int InitPpm(struct ucsi_pd_device *dev)
	{
		return PpmCast(dev)->InitPpmInit();
	}

    protected:
	int StartMainLoop()
	{
		return pthread_create(&thread_, NULL, MainLoop, this);
	}

	int InitPpmInit()
	{
		return ppm_drv_->init_and_wait(ppm_drv_->dev, 2);
	};

	struct ucsi_ppm_driver *GetPpmDrv()
	{
		return ppm_drv_;
	};

	struct ucsi_pd_driver *GetPdDrv()
	{
		return &pd_drv_;
	};

	struct smbus_driver *GetSmbusDrv()
	{
		return &smbus_drv_;
	};

	struct pd_driver_config *GetConfig()
	{
		return &config_;
	};

	int GetActivePortCount()
	{
		return num_ports_;
	};

	static OpmUnitTest *PpmCast(struct ucsi_pd_device *dev)
	{
		return reinterpret_cast<OpmUnitTest *>(dev);
	}

	static int ConfigureLpmIrq(struct ucsi_pd_device *dev)
	{
		return 0;
	}

	static int BlockForInterrupt(struct smbus_device *device)
	{
		while (1)
			sleep_for(milliseconds(1000));
		return 0;
	}

	static void PdCleanup(struct ucsi_pd_driver *driver)
	{
	}

	static void SmbusCleanup(struct smbus_driver *driver)
	{
	}

	struct pd_driver_config config_ = { .max_num_ports = 2,
					    .port_address_map = { 0x67,
								  0x66 } };
	struct ucsi_pd_driver pd_drv_;
	struct ucsi_ppm_driver *ppm_drv_;
	struct smbus_driver smbus_drv_;
	struct um_ppm_cdev *cdev_;
	pthread_t thread_;
	int num_ports_;
};

class Match {
    public:
	static bool MatchCmd(struct ucsi_control *ctrl, uint8_t cmd)
	{
		return ctrl->command == cmd;
	}

	static bool MatchCmdConn(struct ucsi_control *ctrl, uint8_t cmd,
				 uint8_t conn)
	{
		return ctrl->command == cmd &&
		       (ctrl->command_specific[0] & 0x7f) == conn;
	}

	static bool MatchNotificationEnable(struct ucsi_control *ctrl,
					    uint16_t enable_flags)
	{
		uint16_t enable = ctrl->command_specific[1] << 8 |
				  ctrl->command_specific[0];

		return ctrl->command == UCSI_CMD_SET_NOTIFICATION_ENABLE &&
		       enable == enable_flags;
	}

	static bool MatchAckCcCi(struct ucsi_control *ctrl, uint8_t ack_flags)
	{
		return ctrl->command == UCSI_CMD_ACK_CC_CI &&
		       (ctrl->command_specific[0] & 0x3) == ack_flags;
	}

	static bool MatchGetPdos(struct ucsi_control *ctrl, uint8_t conn,
				 uint8_t partner, uint8_t offset,
				 uint8_t number, uint8_t source)
	{
		return ctrl->command == UCSI_CMD_GET_PDOS &&
		       (ctrl->command_specific[0] & 0x7f) == conn &&
		       ((ctrl->command_specific[0] & 0x80) >> 7) == partner &&
		       ctrl->command_specific[1] == offset &&
		       (ctrl->command_specific[2] & 0x3) == number &&
		       ((ctrl->command_specific[2] & 0x4) >> 2) == source;
	}

	static bool MatchGetAltModes(struct ucsi_control *ctrl,
				     uint8_t recipient, uint8_t conn,
				     uint8_t offset, uint8_t number)
	{
		return ctrl->command == UCSI_CMD_GET_ALTERNATE_MODES &&
		       (ctrl->command_specific[0] & 0x7) == recipient &&
		       (ctrl->command_specific[1] & 0x7f) == conn &&
		       ctrl->command_specific[2] == offset &&
		       (ctrl->command_specific[3] & 0x3) == number;
	}

	static bool MatchGetPdMsg(struct ucsi_control *ctrl, uint8_t conn,
				  uint8_t recipient)
	{
		return ctrl->command == UCSI_CMD_GET_PD_MESSAGE &&
		       (ctrl->command_specific[0] & 0x7) == conn &&
		       (((ctrl->command_specific[0] & 0x80) >> 7) |
			((ctrl->command_specific[1] & 0x3) << 1)) == recipient;
	}
};

/* Definitions of matchers */
MATCHER_P(MatchCmd, cmd, "")
{
	return Match::MatchCmd(arg, cmd);
}

MATCHER_P2(MatchCmdConn, cmd, conn, "")
{
	return Match::MatchCmdConn(arg, cmd, conn);
}

MATCHER_P(MatchNotificationEnable, enable_flags, "")
{
	return Match::MatchNotificationEnable(arg, enable_flags);
}

MATCHER_P(MatchAckCcCi, ack_flags, "")
{
	return Match::MatchAckCcCi(arg, ack_flags);
}

MATCHER_P3(MatchGetSourcePdos, conn, offset, number, "")
{
	return Match::MatchGetPdos(arg, conn, 0, offset, number, 1);
}

MATCHER_P3(MatchGetSinkPdos, conn, offset, number, "")
{
	return Match::MatchGetPdos(arg, conn, 0, offset, number, 0);
}

MATCHER_P3(MatchGetPartnerSourcePdos, conn, offset, number, "")
{
	return Match::MatchGetPdos(arg, conn, 1, offset, number, 1);
}

MATCHER_P3(MatchGetPartnerSinkPdos, conn, offset, number, "")
{
	return Match::MatchGetPdos(arg, conn, 1, offset, number, 0);
}

MATCHER_P3(MatchGetConnAltModes, conn, offset, number, "")
{
	return Match::MatchGetAltModes(arg, 0, conn, offset, number);
}

MATCHER_P3(MatchGetSopAltModes, conn, offset, number, "")
{
	return Match::MatchGetAltModes(arg, 1, conn, offset, number);
}

MATCHER_P3(MatchGetSoppAltModes, conn, offset, number, "")
{
	return Match::MatchGetAltModes(arg, 2, conn, offset, number);
}

MATCHER_P2(MatchGetPdMsg, conn, recipient, "")
{
	return Match::MatchGetPdMsg(arg, conn, recipient);
}

/*
 * UCSI 3.0 commands length is 8 bytes
 * The reference commands below do not include the trailing zeros
 * The commands were captured on the Realtek EVB with firmware 0.6.1
 * UCIS specification uses connector naming while Linux uses ports
 * however we will stick to connector naming
 */

/* PPM_RESET command */
Cmd ppm_reset("ppm_reset", vector<uint8_t>({ 0x01 }));

/* SET_NEW_CAM conn 1 command */
Cmd set_ncam_c1("set_ncam_c1", vector<uint8_t>({ 0xf, 0x0, 0x81, 0xff }));

/* SET_NEW_CAM conn 2 command */
Cmd set_ncam_c2("set_ncam_c2", vector<uint8_t>({ 0xf, 0x0, 0x82, 0xff }));

/* SET_NOTIFICATION_ENABLE command */
Cmd set_notification_en_1("set_notification_en_1",
			  vector<uint8_t>({ 0x5, 0x0, 0x1, 0x80 }));

/* ACK_CC_CI command - command completed ack */
Cmd ack_cc_ci("ack_cc_ci", vector<uint8_t>({ 0x4, 0x0, 0x2 }));

/* GET_CAPABILITY command */
Cmd get_caps("get_caps", vector<uint8_t>({ 0x6 }),
	     vector<uint8_t>({ 0x44, 0x1, 0x0, 0x0, 0x2, 0xb4, 0x0, 0x0, 0x3,
			       0x0, 0x20, 0x1, 0x0, 0x3, 0x30, 0x1 }));

/* GET_CONNECTOR_CAPABILITY conn 1 command */
Cmd get_conn_caps_c1("get_conn_caps_c1", vector<uint8_t>({ 0x7, 0x0, 0x1 }),
		     vector<uint8_t>({ 0xe4, 0x37, 0x0, 0x10 }));

/* GET_PDOS conn 1 source command */
Cmd get_pdos_c1_src("get_pdos_c1_src",
		    vector<uint8_t>({ 0x10, 0x0, 0x01, 0x00, 0x07 }),
		    vector<uint8_t>({ 0x2c, 0x91, 0x11, 0x37 }));

/* GET_PDOS conn 1 sink offset 0 command */
Cmd get_pdos_c1_snk_o0("get_pdo_c1_snk_i0",
		       vector<uint8_t>({ 0x10, 0x0, 0x01, 0x00, 0x03 }),
		       vector<uint8_t>({ 0xa, 0x90, 0x1, 0x26, 0xc8, 0xd0, 0x2,
					 0x0, 0xc8, 0xc0, 0x3, 0x0, 0xc8, 0xb0,
					 0x4, 0x0 }));

/* GET_PDOS conn 1 sink offset 4 command */
Cmd get_pdos_c1_snk_o4("get_pdos_c1_snk_o4",
		       vector<uint8_t>({ 0x10, 0x0, 0x01, 0x04, 0x02 }),
		       vector<uint8_t>({ 0x2c, 0x41, 0x6, 0x0, 0xc8, 0x90, 0x41,
					 0x9a }));

/* GET_ALTERNATE_MODES conn 1 offset 0 command */
Cmd get_alt_modes_c1_o0("get_alt_modes_c1_o0",
			vector<uint8_t>({ 0x0c, 0x00, 0x00, 0x01, 0x00, 0x00 }),
			vector<uint8_t>({ 0x87, 0x80, 0x0, 0x0, 0x0, 0x0 }));

/* GET_ALTERNATE_MODES conn 1 offset 1 command */
Cmd get_alt_modes_c1_o1("get_alt_modes_c1_o1",
			vector<uint8_t>({ 0x0c, 0x00, 0x00, 0x01, 0x01, 0x00 }),
			vector<uint8_t>({ 0xef, 0x17, 0x0, 0x0, 0x0, 0x0 }));

/* GET_ALTERNATE_MODES conn 1 offset 2 command */
Cmd get_alt_modes_c1_o2("get_alt_modes_c1_o2",
			vector<uint8_t>({ 0x0c, 0x00, 0x00, 0x01, 0x02, 0x00 }),
			vector<uint8_t>({ 0x1, 0xff, 0x46, 0x1c, 0x0, 0x40 }));

/* GET_CONNECTOR_STATUS conn 1 command */
Cmd get_conn_status_c1("get_conn_status_c1",
		       vector<uint8_t>({ 0x12, 0x00, 0x01 }),
		       vector<uint8_t>({ 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
					 0x1, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0,
					 0x0, 0x0, 0x0, 0x0 }));

/* GET_CONNECTOR_CAPABILITY conn 2 command */
Cmd get_conn_caps_c2("get_conn_caps_c2", vector<uint8_t>({ 0x7, 0x00, 0x2 }),
		     vector<uint8_t>({ 0xe4, 0x37, 0x0, 0x10 }));

/* GET_PDOS conn 2 source command */
Cmd get_pdos_c2_src("get_pdos_c2_src",
		    vector<uint8_t>({ 0x10, 0x00, 0x02, 0x00, 0x07 }),
		    vector<uint8_t>({ 0x2c, 0x91, 0x11, 0x37 }));

/* GET_PDOS conn 2 sink offset 0 command */
Cmd get_pdos_c2_snk_o0("get_pdos_c2_snk_o0",
		       vector<uint8_t>({ 0x10, 0x00, 0x02, 0x00, 0x03 }),
		       vector<uint8_t>({ 0xa, 0x90, 0x1, 0x26, 0xc8, 0xd0, 0x2,
					 0x0, 0xc8, 0xc0, 0x3, 0x0, 0xc8, 0xb0,
					 0x4, 0x0 }));

/* GET_PDOS conn 2 sink offset 4 command */
Cmd get_pdos_c2_snk_o4("get_pdos_c2_snk_o4",
		       vector<uint8_t>({ 0x10, 0x00, 0x02, 0x04, 0x02 }),
		       vector<uint8_t>({ 0x2c, 0x41, 0x6, 0x0, 0xc8, 0x90, 0x41,
					 0x9a }));

/* GET_ALTERNATE_MODES conn 2 offset 0 command */
Cmd get_alt_modes_c2_o0("get_alt_modes_c2_o0",
			vector<uint8_t>({ 0x0c, 0x00, 0x00, 0x02, 0x00, 0x00 }),
			vector<uint8_t>({ 0x87, 0x80, 0x0, 0x0, 0x0, 0x0 }));

/* GET_ALTERNATE_MODE conn 2 offset 1 command */
Cmd get_alt_modes_c2_o1("get_alt_modes_c2_o1",
			vector<uint8_t>({ 0x0c, 0x00, 0x00, 0x02, 0x01, 0x00 }),
			vector<uint8_t>({ 0xef, 0x17, 0x0, 0x0, 0x0, 0x0 }));

/* GET_ALTERNATE_MODE conn 2 offset 2 command */
Cmd get_alt_modes_c2_o2("get_alt_mode_c2_o2",
			vector<uint8_t>({ 0x0c, 0x00, 0x00, 0x02, 0x02, 0x00 }),
			vector<uint8_t>({ 0x1, 0xff, 0x46, 0x1c, 0x0, 0x40 }));

/* GET_CONNECTOR_STATUS conn 2 command */
Cmd get_conn_status_c2("get_conn_status_c2",
		       vector<uint8_t>({ 0x12, 0x00, 0x02 }),
		       vector<uint8_t>({ 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
					 0x1, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0,
					 0x0, 0x0, 0x0, 0x0 }));
/* SET_NOTIFICATION_ENABLE command */
Cmd set_notification_en_2("set_notification_en_2",
			  vector<uint8_t>({ 0x5, 0x0, 0xe7, 0xdb }));

/* Messages when LPM alert after connecting partner to connector 1 happens */

/* GET_CONNECTOR_STATUS conn 1 update 1 command */
Cmd get_conn_status_c1_update1(
	"get_conn_status_c1_update1", vector<uint8_t>({ 0x12, 0x00, 0x01 }),
	vector<uint8_t>({ 0x0, 0x40, 0x3d, 0x40, 0x0, 0x0, 0x0, 0x0, 0x8, 0xc0,
			  0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }));

/* GET_CONNECTOR_STATUS conn 1 update 2 command */
Cmd get_conn_status_c1_update2("get_conn_status_c1_update2",
			       vector<uint8_t>({ 0x12, 0x00, 0x01 }),
			       vector<uint8_t>({ 0x60, 0x0, 0x3b, 0x40, 0x5a,
						 0x68, 0x1, 0x13, 0x8, 0xc0,
						 0x40, 0x0, 0x0, 0x0, 0x0, 0x0,
						 0x0, 0x0, 0x0 }));

/* GET_CONNECTOR_STATUS conn 1 update 3 command */
Cmd get_conn_status_c1_update3("get_conn_status_c1_update3",
			       vector<uint8_t>({ 0x12, 0x00, 0x01 }),
			       vector<uint8_t>({ 0x0, 0x10, 0x2b, 0x40, 0x5a,
						 0x68, 0x1, 0x13, 0x1, 0xc0,
						 0x40, 0x0, 0x0, 0x0, 0x0, 0x0,
						 0x0, 0x0, 0x0 }));

/* GET_CONNECTOR_STATUS conn 1 update 4 command */
Cmd get_conn_status_c1_update4("get_conn_status_c1_update4",
			       vector<uint8_t>({ 0x12, 0x00, 0x01 }),
			       vector<uint8_t>({ 0x60, 0x2, 0x2b, 0x40, 0x2c,
						 0xb1, 0x84, 0x43, 0x1, 0xc0,
						 0x40, 0x0, 0x0, 0x0, 0x0, 0x0,
						 0x0, 0x0, 0x0 }));

/* GET_CONNECTOR_STATUS conn 1 update 5 command */
Cmd get_conn_status_c1_update5("get_conn_status_p1_update5",
			       vector<uint8_t>({ 0x12, 0x00, 0x01 }),
			       vector<uint8_t>({ 0x0, 0x1, 0x4b, 0x40, 0x2c,
						 0xb1, 0x84, 0x43, 0x1, 0xc0,
						 0x40, 0x0, 0x0, 0x0, 0x0, 0x0,
						 0x0, 0x0, 0x0 }));

/* GET_PDOS conn 1 partner source offset 0 command */
Cmd get_pdos_c1_partner_src_o0(
	"get_pdos_c1_partner_src_o0",
	vector<uint8_t>({ 0x10, 0x00, 0x81, 0x00, 0x07 }),
	vector<uint8_t>({ 0x2c, 0x91, 0x1, 0x2e, 0x2c, 0xd1, 0x2, 0x0, 0x2c,
			  0xb1, 0x4, 0x0, 0x2c, 0x41, 0x6, 0x0 }));

/* GET_PDOS conn 1 partner source offset 4 command */
Cmd get_pdos_c1_partner_src_o4("get_pdos_c1_partner_src_o4",
			       vector<uint8_t>({ 0x10, 0x00, 0x81, 0x04,
						 0x06 }));

/* GET_ALTERNATE_MODES sop offset 0 command */
Cmd get_alt_modes_sop_o0("get_alt_modes_sop_o0",
			 vector<uint8_t>({ 0x0c, 0x00, 0x01, 0x01, 0x00,
					   0x00 }),
			 vector<uint8_t>({ 0x1, 0xff, 0x45, 0x0, 0x1c, 0x0 }));

/* GET_ALTERNATE_MODES sop offset 1 command */
Cmd get_alt_modes_sop_o1("get_alt_modes_sop_o1",
			 vector<uint8_t>({ 0x0c, 0x00, 0x01, 0x01, 0x01,
					   0x00 }));

/* GET_ALTERNATE_MODES sopp offset 0 command */
Cmd get_alt_modes_sopp_o0("get_alt_modes_sopp_o0",
			  vector<uint8_t>({ 0x0c, 0x00, 0x2, 0x01, 0x00,
					    0x00 }));
/* GET_CURRENT_CAM command */
Cmd get_current_cam_c1("get_current_cam_c1", vector<uint8_t>({ 0xe, 0x0, 0x1 }),
		       vector<uint8_t>({ 0x1 }));

/* GET_CABLE_PROPERTY command */
Cmd get_cable_prop_c1("get_cable_prop_c1", vector<uint8_t>({ 0x11, 0x0, 0x1 }),
		      vector<uint8_t>({ 0x03, 0x00, 0x32, 0x90, 0x01 }));

/* GET_PD_MESSAGE sop command */
Cmd get_pd_msg_sop_c1(
	"get_pd_msg_sop_c1",
	vector<uint8_t>({ 0x15, 0x0, 0x81, 0x0, 0x70, 0x10, 0x0, 0x0 }),
	vector<uint8_t>({ 0x3c, 0x41, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x00,
			  0x12, 0x07, 0x69, 0xb0, 0x01, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }));

/* GET_PD_MESSAGE sopp command */
Cmd get_pd_msg_sopp_c1(
	"get_pd_msg_sopp_c1",
	vector<uint8_t>({ 0x15, 0x0, 0x1, 0x1, 0x70, 0x10, 0x0, 0x0 }),
	vector<uint8_t>({ 0x3c, 0x41, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x00,
			  0x12, 0x07, 0x69, 0xb0, 0x01, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }));

/* GET_PDOS conn 1 partner sink command */
Cmd get_pdos_c1_partner_snk("get_pdos_c1_partner_snk",
			    vector<uint8_t>({ 0x10, 0x00, 0x81, 0x00, 0x03 }),
			    vector<uint8_t>({ 0xa, 0x90, 0x1, 0x3e }));

/* ACK_CC_CI command - command connector change ack */
Cmd ack_cc_ci_conn("ack_cc_ci_conn", vector<uint8_t>({ 0x4, 0x00, 0x1 }));

/* Messages when LPM alert after disconnecting partner from port 1 happens */

/* GET_CONNECTOR_STATUS conn 1 command on disconnect */
Cmd get_conn_status_c1_disconnect(
	"get_conn_status_c1_disconnect", vector<uint8_t>({ 0x12, 0x00, 0x01 }),
	vector<uint8_t>({ 0x0, 0x41, 0x3, 0x40, 0x0, 0x0, 0x0, 0x0, 0x1, 0xc0,
			  0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }));
/*
 * Initiate OPM initialization sequence which discovers properties
 * of local ports and verify the properties created by the OPM.
 */
TEST_F(OpmUnitTest, OpmInitialization)
{
	Cmd::ExecuteCmdPtr fptr = &Cmd::ExecuteCmd;
	SysfsVerifier verifier(num_ports_);

	/* Make sure that we fail when no handler is set for a command */
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, _, _))
		.WillRepeatedly(Cmd::ErrorNoHandlerForCmd);

	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmd(UCSI_CMD_PPM_RESET), _))
		.WillRepeatedly(Invoke(&ppm_reset, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_SET_NEW_CAM, 1), _))
		.WillRepeatedly(Invoke(&set_ncam_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_SET_NEW_CAM, 2), _))
		.WillRepeatedly(Invoke(&set_ncam_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchNotificationEnable(0x8001), _))
		.WillRepeatedly(Invoke(&set_notification_en_1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchAckCcCi(0x2), _))
		.WillRepeatedly(Invoke(&ack_cc_ci, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchAckCcCi(0x1), _))
		.WillRepeatedly(Invoke(&ack_cc_ci_conn, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmd(UCSI_CMD_GET_CAPABILITY), _))
		.WillRepeatedly(Invoke(&get_caps, fptr));

	EXPECT_CALL(
		*pd_driver_mock_,
		execute_cmd(_,
			    MatchCmdConn(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 1),
			    _))
		.WillRepeatedly(Invoke(&get_conn_caps_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSourcePdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_src, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_snk_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(1, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_snk_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 2, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_,
				MatchCmdConn(UCSI_CMD_GET_CONNECTOR_STATUS, 1),
				_))
		.WillRepeatedly(Invoke(&get_conn_status_c1, fptr));

	EXPECT_CALL(
		*pd_driver_mock_,
		execute_cmd(_,
			    MatchCmdConn(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 2),
			    _))
		.WillRepeatedly(Invoke(&get_conn_caps_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSourcePdos(2, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_src, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(2, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_snk_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(2, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_snk_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 2, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_,
				MatchCmdConn(UCSI_CMD_GET_CONNECTOR_STATUS, 2),
				_))
		.WillRepeatedly(Invoke(&get_conn_status_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchNotificationEnable(0xdbe7), _))
		.WillRepeatedly(Invoke(&set_notification_en_2, fptr));

	/* We can start now because expectations are set */
	EXPECT_EQ(0, StartMainLoop());

	/* Verify ports properties */
	verifier.VerifyPortProperties(1);
	verifier.VerifyPortProperties(2);
	/* Initialization is completed now ;) */
}

/*
 * Emit an LPM alert and connector status marking a port partner as connected
 * and confirm the OPM fills out all the relevant partner properties.
 */
TEST_F(OpmUnitTest, VerifyPartnerPropertiesOnConnect)
{
	Cmd::ExecuteCmdPtr fptr = &Cmd::ExecuteCmd;
	SysfsVerifier verifier(num_ports_);

	/* Make sure that we fail when no handler is set for a command */
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, _, _))
		.WillRepeatedly(Cmd::ErrorNoHandlerForCmd);

	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmd(UCSI_CMD_PPM_RESET), _))
		.WillRepeatedly(Invoke(&ppm_reset, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_SET_NEW_CAM, 1), _))
		.WillRepeatedly(Invoke(&set_ncam_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_SET_NEW_CAM, 2), _))
		.WillRepeatedly(Invoke(&set_ncam_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchNotificationEnable(0x8001), _))
		.WillRepeatedly(Invoke(&set_notification_en_1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchAckCcCi(0x2), _))
		.WillRepeatedly(Invoke(&ack_cc_ci, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchAckCcCi(0x1), _))
		.WillRepeatedly(Invoke(&ack_cc_ci_conn, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmd(UCSI_CMD_GET_CAPABILITY), _))
		.WillRepeatedly(Invoke(&get_caps, fptr));

	EXPECT_CALL(
		*pd_driver_mock_,
		execute_cmd(_,
			    MatchCmdConn(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 1),
			    _))
		.WillRepeatedly(Invoke(&get_conn_caps_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSourcePdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_src, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_snk_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(1, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_snk_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 2, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o2, fptr));

	EXPECT_CALL(
		*pd_driver_mock_,
		execute_cmd(_,
			    MatchCmdConn(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 2),
			    _))
		.WillRepeatedly(Invoke(&get_conn_caps_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSourcePdos(2, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_src, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(2, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_snk_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(2, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_snk_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 2, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_,
				MatchCmdConn(UCSI_CMD_GET_CONNECTOR_STATUS, 2),
				_))
		.WillRepeatedly(Invoke(&get_conn_status_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchNotificationEnable(0xdbe7), _))
		.WillRepeatedly(Invoke(&set_notification_en_2, fptr));
	/*
	 * Make sure that all get connector 1 status commands are handled under
	 * one expect_call because request command is fixed but replies change
	 */
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_,
				MatchCmdConn(UCSI_CMD_GET_CONNECTOR_STATUS, 1),
				_))
		.WillOnce(Invoke(&get_conn_status_c1, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update1, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update1, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update2, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update2, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update3, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update3, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update4, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update4, fptr))
		.WillRepeatedly(Invoke(&get_conn_status_c1_update5, fptr));

	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSopAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_sop_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSopAltModes(1, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_sop_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSoppAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_sopp_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetPartnerSourcePdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_partner_src_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetPartnerSourcePdos(1, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_partner_src_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetPartnerSinkPdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_partner_snk, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_GET_CURRENT_CAM, 1),
				_))
		.WillRepeatedly(Invoke(&get_current_cam_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_GET_CABLE_PROPERTY, 1),
				_))
		.WillRepeatedly(Invoke(&get_cable_prop_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchGetPdMsg(1, 1), _))
		.WillRepeatedly(Invoke(&get_pd_msg_sop_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchGetPdMsg(1, 2), _))
		.WillRepeatedly(Invoke(&get_pd_msg_sopp_c1, fptr));

	/* We can start now because expectations are set */
	EXPECT_EQ(0, StartMainLoop());

	/* Verify ports properties */
	verifier.VerifyPortProperties(1);
	verifier.VerifyPortProperties(2);
	/* Initialization is completed now ;) */

	/*
	 * When partner is connected then Realtek triggers a number of
	 * consecutive interrupts, simulate connecting partner to port
	 */
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));

	/* Verify port partner and cable properties */
	verifier.VerifyPortPartnerProperties(1);
	verifier.VerifyPortCableProperties(1);
}

/*
 * Emit an LPM alert and connector status marking a port partner as connected
 * and confirm the OPM fills out all the relevant partner properties. Then
 * emit the LPM alert once again and connector status marking the port partner
 * as disconnected and confirm the OPM removes partner properties.
 */
TEST_F(OpmUnitTest, VerifyPartnerPropertiesOnConnectAndDisconnect)
{
	Cmd::ExecuteCmdPtr fptr = &Cmd::ExecuteCmd;
	SysfsVerifier verifier(num_ports_);

	/* Make sure that we fail when no handler is set for a command */
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, _, _))
		.WillRepeatedly(Cmd::ErrorNoHandlerForCmd);

	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmd(UCSI_CMD_PPM_RESET), _))
		.WillRepeatedly(Invoke(&ppm_reset, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_SET_NEW_CAM, 1), _))
		.WillRepeatedly(Invoke(&set_ncam_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_SET_NEW_CAM, 2), _))
		.WillRepeatedly(Invoke(&set_ncam_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchNotificationEnable(0x8001), _))
		.WillRepeatedly(Invoke(&set_notification_en_1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchAckCcCi(0x2), _))
		.WillRepeatedly(Invoke(&ack_cc_ci, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchAckCcCi(0x1), _))
		.WillRepeatedly(Invoke(&ack_cc_ci_conn, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmd(UCSI_CMD_GET_CAPABILITY), _))
		.WillRepeatedly(Invoke(&get_caps, fptr));

	EXPECT_CALL(
		*pd_driver_mock_,
		execute_cmd(_,
			    MatchCmdConn(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 1),
			    _))
		.WillRepeatedly(Invoke(&get_conn_caps_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSourcePdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_src, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_snk_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(1, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_snk_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(1, 2, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c1_o2, fptr));
	EXPECT_CALL(
		*pd_driver_mock_,
		execute_cmd(_,
			    MatchCmdConn(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 2),
			    _))
		.WillRepeatedly(Invoke(&get_conn_caps_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSourcePdos(2, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_src, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(2, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_snk_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSinkPdos(2, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c2_snk_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetConnAltModes(2, 2, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_c2_o2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_,
				MatchCmdConn(UCSI_CMD_GET_CONNECTOR_STATUS, 2),
				_))
		.WillRepeatedly(Invoke(&get_conn_status_c2, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchNotificationEnable(0xdbe7), _))
		.WillRepeatedly(Invoke(&set_notification_en_2, fptr));
	/*
	 * Make sure that all get connector 1 status commands are handled under
	 * one expect_call because request command is fixed but replies change
	 */
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_,
				MatchCmdConn(UCSI_CMD_GET_CONNECTOR_STATUS, 1),
				_))
		.WillOnce(Invoke(&get_conn_status_c1, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update1, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update1, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update2, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update2, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update3, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update3, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update4, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update4, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update5, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update5, fptr))
		.WillOnce(Invoke(&get_conn_status_c1_update5, fptr))
		.WillRepeatedly(Invoke(&get_conn_status_c1_disconnect, fptr));

	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSopAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_sop_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSopAltModes(1, 1, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_sop_o1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetSoppAltModes(1, 0, 0), _))
		.WillRepeatedly(Invoke(&get_alt_modes_sopp_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetPartnerSourcePdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_partner_src_o0, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetPartnerSourcePdos(1, 4, 2), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_partner_src_o4, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchGetPartnerSinkPdos(1, 0, 3), _))
		.WillRepeatedly(Invoke(&get_pdos_c1_partner_snk, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_GET_CURRENT_CAM, 1),
				_))
		.WillRepeatedly(Invoke(&get_current_cam_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_,
		    execute_cmd(_, MatchCmdConn(UCSI_CMD_GET_CABLE_PROPERTY, 1),
				_))
		.WillRepeatedly(Invoke(&get_cable_prop_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchGetPdMsg(1, 1), _))
		.WillRepeatedly(Invoke(&get_pd_msg_sop_c1, fptr));
	EXPECT_CALL(*pd_driver_mock_, execute_cmd(_, MatchGetPdMsg(1, 2), _))
		.WillRepeatedly(Invoke(&get_pd_msg_sopp_c1, fptr));

	/* We can start now because expectations are set */
	EXPECT_EQ(0, StartMainLoop());

	/* Verify ports properties */
	verifier.VerifyPortProperties(1);
	verifier.VerifyPortProperties(2);
	/* Initialization is completed now ;) */

	/*
	 * When partner is connected then Realtek triggers a number of
	 * consecutive interrupts, simulate connecting partner to port
	 */
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));

	/* Verify port partner and cable properties */
	verifier.VerifyPortPartnerProperties(1);
	verifier.VerifyPortCableProperties(1);

	/* Simulate parter disconnect from port */
	ppm_drv_->lpm_alert(ppm_drv_->dev, 1);
	sleep_for(milliseconds(250));

	/* Verify port and cable disconnect */
	verifier.VerifyPortPartnerExistence(1);
	verifier.VerifyPortCableExistence(1);
}
