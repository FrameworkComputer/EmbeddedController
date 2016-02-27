/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <common/image.h>
#include <common/publickey.h>
#include <common/signed_header.h>
#ifdef HAVE_JSON
#include <rapidjson/document.h>
#endif

#include <map>
#include <string>
#include <vector>

#include <fstream>
#include <iostream>
#include <sstream>

#include <libxml/parser.h>
#include <libxml/tree.h>

using namespace std;

#define VERBOSE(...)                                 \
  do {                                               \
    if (FLAGS_verbose) fprintf(stderr, __VA_ARGS__); \
  } while (0)
#define FATAL(...)                \
  do {                            \
    fprintf(stderr, __VA_ARGS__); \
    abort();                      \
  } while (0)

bool FLAGS_verbose = false;
bool FLAGS_cros = false;
int last_logical_offset = -1;
int fuse_index = 0;

// Brute xml parsing.
// Find HashItem w/ key == name, return val field, recursively.
static xmlChar* get_val(xmlNodePtr node, const char* key) {
  xmlNode* cur_node = NULL;
  xmlChar* val = NULL;

  for (cur_node = node->children; cur_node; cur_node = cur_node->next) {
    if (!strcmp("HashItem", (const char*)(cur_node->name))) {
      // Hardcode parse <HashItem><Key>key</Key><Val>val</Val></HashItem>
      xmlNodePtr key_node = cur_node->children->next;
      xmlNodePtr val_node = cur_node->children->next->next->next;
      xmlChar* keyName = xmlNodeGetContent(key_node);
      xmlChar* valData = xmlNodeGetContent(val_node);

      if (!strcmp(key, (const char*)keyName)) {
        // Found our key, save val and done.
        xmlFree(keyName);
        val = valData;
        break;
      }

      xmlFree(valData);
      xmlFree(keyName);
    }

    val = get_val(cur_node, key);
    if (val) {
      // Found our key somewhere deeper down; done.
      break;
    }
  }

  return val;
}

static bool get_fuse(xmlNodePtr a_node, map<string, uint32_t>* ids,
                     map<string, uint32_t>* bits) {
  bool result = false;

  // Interested in <HashType>
  if (strcmp("HashType", (const char*)(a_node->name))) {
    return result;
  }

  // Values we are interested in.
  xmlChar* RegName = get_val(a_node, "RegName");
  xmlChar* Width = get_val(a_node, "Width");
  xmlChar* FuseLogicalOffset = get_val(a_node, "FuseLogicalOffset");

  // Track 1024 fuses at most.
  int fuseLogicalOffset = atoi((const char*)FuseLogicalOffset);
  if (fuseLogicalOffset >= last_logical_offset) {
    last_logical_offset = fuseLogicalOffset;
    ids->insert(make_pair((const char*)RegName, fuse_index++));
    bits->insert(make_pair((const char*)RegName, atoi((const char*)Width)));
  } else {
    // Logical offset is regressing; assume we saw all the fuses.
    // There are multiple sections that list all the fuses in the xml;
    // we only care about parsing them once.
    result = true;
  }

  xmlFree(FuseLogicalOffset);
  xmlFree(Width);
  xmlFree(RegName);

  return result;
}

static bool find_fuses(xmlNodePtr a_node, map<string, uint32_t>* ids,
                       map<string, uint32_t>* bits) {
  xmlNode* cur_node = NULL;
  bool done = false;

  for (cur_node = a_node; !done && cur_node; cur_node = cur_node->next) {
    xmlChar* content = NULL;

    if (cur_node->type == XML_TEXT_NODE &&
        (content = xmlNodeGetContent(cur_node)) != NULL) {
      if (!strcmp("FuseLogicalOffset", (const char*)content)) {
        // Found a likely fuse definition section; collect it.
        done = get_fuse(a_node->parent->parent->parent, ids, bits);
      }
    }

    if (content) xmlFree(content);

    if (!done && cur_node->children) {
      done = find_fuses(cur_node->children, ids, bits);
    }
  }

  return done;
}

static bool find_default_reg_value(xmlNodePtr a_node, const string& regname,
                                   string* result) {
  xmlNode* cur_node = NULL;
  bool done = false;

  for (cur_node = a_node; !done && cur_node; cur_node = cur_node->next) {
    xmlChar* content = NULL;

    if (cur_node->type == XML_TEXT_NODE &&
        (content = xmlNodeGetContent(cur_node)) != NULL) {
      if (!strcmp(regname.c_str(), (const char*)content)) {
        xmlChar* val = get_val(cur_node->parent->parent->parent, "Default");
        if (val) {
          result->assign((const char*)val);
          xmlFree(val);
          done = true;
        }
      }
    }

    if (content) xmlFree(content);

    if (!done && cur_node->children) {
      done = find_default_reg_value(cur_node->children, regname, result);
    }
  }

  return done;
}

// Read XML, populate two maps, name -> val
bool readXML(const string& filename, map<string, uint32_t>* ids,
             map<string, uint32_t>* bits, uint32_t* p4cl) {
  bool result = false;
  LIBXML_TEST_VERSION

  xmlDocPtr doc = xmlReadFile(filename.c_str(), NULL, 0);

  if (doc) {
    result = find_fuses(xmlDocGetRootElement(doc), ids, bits);
    string p4clStr;
    result &= find_default_reg_value(xmlDocGetRootElement(doc),
                                     "SWDP_P4_LAST_SYNC", &p4clStr);
    if (result) {
      *p4cl = atoi(p4clStr.c_str());
    }
    xmlFreeDoc(doc);
  }

  xmlCleanupParser();
  xmlMemoryDump();

  return result;
}

// Read JSON, populate map, name -> val
bool readJSON(const string& filename, string* tag,
              map<string, uint32_t>* values, map<string, uint32_t>* fusemap,
              map<string, uint32_t>* infomap) {
  bool result = false;
#ifdef HAVE_JSON
  ifstream ifs(filename.c_str());
  if (ifs) {
    // Touch up a bit to allow for comments.
    // Beware: we drop everything past and including '//' from any line.
    //         Thus '//' cannot be substring of any value..
    string s;
    while (ifs) {
      string line;
      getline(ifs, line);
      size_t slash = line.find("//");
      if (slash != string::npos) {
        line.erase(slash);
      }
      s.append(line);
    }

    // Try parse.
    rapidjson::Document d;
    if (d.Parse(s.c_str()).HasParseError()) {
      FATAL("JSON %s[%lu]: parse error\n", filename.c_str(),
            d.GetErrorOffset());
    } else {
#define CHECKVALUE(x)                               \
  do {                                              \
    if (!d.HasMember(x)) {                          \
      FATAL("manifest is lacking field '%s'\n", x); \
    };                                              \
  } while (0)

#define GETVALUE(x)                                 \
  do {                                              \
    if (!d.HasMember(x)) {                          \
      FATAL("manifest is lacking field '%s'\n", x); \
    };                                              \
    (*values)[x] = d[x].GetInt();                   \
  } while (0)

      CHECKVALUE("fuses");
      const rapidjson::Document::ValueType& fuses = d["fuses"];
      for (rapidjson::Value::ConstMemberIterator it = fuses.MemberBegin();
           it != fuses.MemberEnd(); ++it) {
        (*fusemap)[it->name.GetString()] = it->value.GetInt();
      }

      CHECKVALUE("info");
      const rapidjson::Document::ValueType& infos = d["info"];
      for (rapidjson::Value::ConstMemberIterator it = infos.MemberBegin();
           it != infos.MemberEnd(); ++it) {
        (*infomap)[it->name.GetString()] = it->value.GetInt();
      }

      GETVALUE("keyid");
      GETVALUE("p4cl");
      GETVALUE("epoch");
      GETVALUE("major");
      GETVALUE("minor");
      GETVALUE("applysec");
      GETVALUE("config1");
      GETVALUE("err_response");
      GETVALUE("expect_response");
      GETVALUE("timestamp");

      CHECKVALUE("tag");
      const rapidjson::Document::ValueType& Tag = d["tag"];
      tag->assign(Tag.GetString());

      result = true;

#undef GETVALUE
#undef CHECKVALUE
    }
  }
#endif  // HAVE_JSON
  return result;
}

string inputFilename;
string outputFilename;
string keyFilename;
string xmlFilename;
string jsonFilename;
string outputFormat;
string signatureFilename;
string hashesFilename;
bool fillPattern = false;
uint32_t pattern = -1;
bool fillRandom = false;

void usage(int argc, char* argv[]) {
  fprintf(stderr,
          "Usage: %s options\n"
          "--input=$elf-filename\n"
          "--output=output-filename\n"
          "--key=$pem-filename\n"
          "[--cros] to sign for the ChromeOS realm w/o manifest\n"
          "[--xml=$xml-filename] typically 'havenTop.xml'\n"
          "[--json=$json-filename] the signing manifest\n"
          "[--format=bin|hex] output file format, hex is default\n"
          "[--signature=$sig-filename] replace signature with file content\n"
          "[--hashes=$hashes-filename] destination file for intermediary "
          "hashes to be signed\n"
          "[--randomfill] to pad image to 512K with random bits\n"
          "[--patternfill=N] to pad image to 512K with pattern N\n"
          "[--verbose]\n",
          argv[0]);
}

int getOptions(int argc, char* argv[]) {
  static struct option long_options[] = {
      // name, has_arg
      {"cros", no_argument, NULL, 'c'},
      {"format", required_argument, NULL, 'f'},
      {"help", no_argument, NULL, 'h'},
      {"input", required_argument, NULL, 'i'},
      {"json", required_argument, NULL, 'j'},
      {"key", required_argument, NULL, 'k'},
      {"output", required_argument, NULL, 'o'},
      {"verbose", no_argument, NULL, 'v'},
      {"xml", required_argument, NULL, 'x'},
      {"signature", required_argument, NULL, 's'},
      {"hashes", required_argument, NULL, 'H'},
      {"randomfill", no_argument, NULL, 'r'},
      {"patternfill", required_argument, NULL, 'p'},
      {"writefuses", required_argument, NULL, 'w'},
      {0, 0, 0, 0}};
  int c, option_index = 0;
  outputFormat.assign("hex");
  while ((c = getopt_long(argc, argv, "i:o:p:k:x:j:f:s:H:chvr", long_options,
                          &option_index)) != -1) {
    switch (c) {
      case 0:
        fprintf(stderr, "option %s", long_options[option_index].name);
        if (optarg) fprintf(stderr, " with arg %s", optarg);
        fprintf(stderr, "\n");
        break;
      case 'c':
        FLAGS_cros = true;
        break;
      case 'i':
        inputFilename.assign(optarg);
        break;
      case 'o':
        outputFilename.assign(optarg);
        break;
      case 'k':
        keyFilename.assign(optarg);
        break;
      case 'x':
        xmlFilename.assign(optarg);
        break;
      case 's':
        signatureFilename.assign(optarg);
        break;
      case 'j':
        jsonFilename.assign(optarg);
        break;
      case 'f':
        outputFormat.assign(optarg);
        break;
      case 'H':
        hashesFilename.assign(optarg);
        break;
      case 'r':
        fillRandom = true;
        break;
      case 'p':
        fillPattern = true;
        pattern = strtoul(optarg, NULL, 0);
        break;
      case 'h':
        usage(argc, argv);
        return 1;

      case 'v':
        FLAGS_verbose = true;
        break;
      case '?':
        // getopt_long printed error
        return 1;
    }
  }
  if (inputFilename.empty() || outputFilename.empty() || keyFilename.empty() ||
      ((outputFormat != "bin") && (outputFormat != "hex"))) {
    usage(argc, argv);
    return 1;
  }
  return 0;
}

int main(int argc, char* argv[]) {
  if (getOptions(argc, argv)) {
    exit(1);
  }

  PublicKey key(keyFilename);
  if (!key.ok()) return -1;

  // Load elf.
  Image image;
  if (!image.fromElf(inputFilename)) return -2;

  if (fillPattern) image.fillPattern(pattern);
  if (fillRandom) image.fillRandom();

  SignedHeader hdr;

  hdr.keyid = key.n0inv();

  hdr.ro_base = image.ro_base();
  hdr.ro_max = image.ro_max();
  hdr.rx_base = image.rx_base();
  hdr.rx_max =
      image.rx_max() +
      12;  // TODO: m3 instruction prefetch sets off GLOBALSEC when too tight
           //       make sure these are nops or such?
  hdr.timestamp_ = time(NULL);

  // Parse signing manifest.
  map<string, uint32_t> values;
  map<string, uint32_t> fuses;
  map<string, uint32_t> infos;
  string tag;

  if (jsonFilename.empty()) {
    // Defaults, in case no JSON
    values.insert(make_pair("keyid", key.n0inv()));
    values.insert(make_pair("epoch", 0x1337));
  }

  // Hardcoded expectation. Can be overwritten in JSON w/ new explicit value.
  fuses["FW_DEFINED_DATA_EXTRA_BLK6"] = 0;

  if (!jsonFilename.empty() &&
      !readJSON(jsonFilename, &tag, &values, &fuses, &infos)) {
    FATAL("Failed to read JSON from '%s'\n", jsonFilename.c_str());
  }

  // Fill in more of hdr, per manifest values
  for (map<string, uint32_t>::const_iterator it = values.begin();
       it != values.end(); ++it) {
    VERBOSE("%s : %u\n", it->first.c_str(), it->second);
  }

  hdr.p4cl_ = values["p4cl"];
  hdr.epoch_ = values["epoch"];
  hdr.major_ = values["major"];
  hdr.minor_ = values["minor"];
  hdr.applysec_ = values["applysec"];
  hdr.config1_ = values["config1"];
  hdr.err_response_ = values["err_response"];
  hdr.expect_response_ = values["expect_response"];
  if (values["timestamp"]) hdr.timestamp_ = values["timestamp"];

  VERBOSE("timestamp: %ld\n", hdr.timestamp_);

  // Check keyId.
  if (values["keyid"] != hdr.keyid) {
    FATAL("mismatched keyid JSON %d vs. key %d\n", values["keyid"], hdr.keyid);
  }

  if (FLAGS_cros) {
    if (!tag.empty()) {
      FATAL("--cros whilst also specifying tag per manifest is a no go");
    }
    tag = "\x01\x00\x00\x00";  // cros realm identifier in rwr[0]
  }

  // Fill in tag.
  VERBOSE("tag: \"%s\"\n", tag.c_str());
  strncpy((char*)(&hdr.tag), tag.c_str(), sizeof(hdr.tag));

  // List the specific fuses and values.
  VERBOSE("care about %lu fuses:\n", fuses.size());
  for (map<string, uint32_t>::const_iterator it = fuses.begin();
       it != fuses.end(); ++it) {
    VERBOSE("fuse '%s' should have value %u\n", it->first.c_str(), it->second);
  }

  // Parse xml.
  map<string, uint32_t> fuse_ids;
  map<string, uint32_t> fuse_bits;
  uint32_t xml_p4cl = 0;

  if (!xmlFilename.empty() &&
      !readXML(xmlFilename, &fuse_ids, &fuse_bits, &xml_p4cl)) {
    FATAL("Failed to read XML from '%s'\n", xmlFilename.c_str());
  }

  if (values["p4cl"] != xml_p4cl) {
    FATAL("mismatching p4cl: xml %u vs. json %u\n", xml_p4cl, values["p4cl"]);
  }

  VERBOSE("found %lu fuse definitions\n", fuse_ids.size());
  assert(fuse_ids.size() < FUSE_MAX);

  if (fuse_ids.size() != 0) {
    // Make sure FW_DEFINED_DATA_EXTRA_BLK6 is still at 125, width 3.
    assert(fuse_ids["FW_DEFINED_DATA_EXTRA_BLK6"] == 125);
    assert(fuse_bits["FW_DEFINED_DATA_EXTRA_BLK6"] == 5);
  }

  // Whether we loaded xml or not, hardcode FW_DEFINED_DATA_EXTRA_BLK6
  fuse_ids["FW_DEFINED_DATA_EXTRA_BLK6"] = 125;
  fuse_bits["FW_DEFINED_DATA_EXTRA_BLK6"] = 5;

  for (map<string, uint32_t>::const_iterator it = fuse_ids.begin();
       it != fuse_ids.end(); ++it) {
    VERBOSE("fuse '%s' at %u, width %u\n", it->first.c_str(), it->second,
            fuse_bits[it->first]);
  }

  // Compute fuse_values array, according to manifest and xml.
  uint32_t fuse_values[FUSE_MAX];
  for (size_t i = 0; i < FUSE_MAX; ++i) fuse_values[i] = FUSE_IGNORE;

  for (map<string, uint32_t>::const_iterator x = fuses.begin();
       x != fuses.end(); ++x) {
    map<string, uint32_t>::const_iterator it = fuse_ids.find(x->first);
    if (it == fuse_ids.end()) {
      FATAL("cannot find definition for fuse '%s'\n", x->first.c_str());
    }
    uint32_t idx = it->second;
    assert(idx < FUSE_MAX);
    uint32_t mask = (1ul << fuse_bits[x->first]) - 1;
    if ((x->second & mask) != x->second) {
      FATAL("specified fuse value too large\n");
    }
    uint32_t val = FUSE_PADDING & ~mask;
    val |= x->second;

    fuse_values[idx] = val;
    hdr.markFuse(idx);
  }

  // Print out fuse hash input.
  VERBOSE("expected fuse state:\n");
  for (size_t i = 0; i < FUSE_MAX; ++i) {
    VERBOSE("%08x ", fuse_values[i]);
  }
  VERBOSE("\n");

  // Compute info_values array, according to manifest.
  uint32_t info_values[INFO_MAX];
  for (size_t i = 0; i < INFO_MAX; ++i) info_values[i] = INFO_IGNORE;

  for (map<string, uint32_t>::const_iterator x = infos.begin();
       x != infos.end(); ++x) {
    uint32_t index = atoi(x->first.c_str());
    assert(index < INFO_MAX);

    info_values[index] ^= x->second;

    hdr.markInfo(index);
  }

  // TODO: read values from JSON or implement version logic here.

  // Print out info hash input.
  VERBOSE("expected info state:\n");
  for (size_t i = 0; i < INFO_MAX; ++i) {
    VERBOSE("%08x ", info_values[i]);
  }
  VERBOSE("\n");

  if (!signatureFilename.empty()) {
    int fd = ::open(signatureFilename.c_str(), O_RDONLY);
    if (fd > 0) {
      int n = ::read(fd, hdr.signature, sizeof(hdr.signature));
      ::close(fd);

      if (n != sizeof(hdr.signature))
        FATAL("cannot read from '%s'\n", signatureFilename.c_str());

      VERBOSE("provided signature\n");
    } else {
      FATAL("cannot open '%s'\n", signatureFilename.c_str());
    }
  }

  // Sign image.
  if (image.sign(key, &hdr, fuse_values, info_values, hashesFilename)) {
    image.generate(outputFilename, outputFormat == "hex");
  } else {
    FATAL("failed to sign\n");
  }

  return 0;
}
