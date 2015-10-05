/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>

#include <common/image.h>
#include <common/publickey.h>
#include <common/signed_header.h>
#ifdef HAVE_JSON
#include <rapidjson/document.h>
#endif

#include <string>
#include <map>
#include <vector>

#include <iostream>
#include <fstream>
#include <sstream>

#include <libxml/parser.h>
#include <libxml/tree.h>

using namespace std;

int last_logical_offset = -1;
int fuse_index = 0;

// Brute xml parsing.
// Find HashItem w/ key == name, return val field, recursively.
static
xmlChar* get_val(xmlNodePtr node, const char* key) {
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

static
bool print_fuse(xmlNodePtr a_node,
                map<string, uint32_t>* ids, map<string, uint32_t>* bits) {
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

static
bool find_fuses(xmlNodePtr a_node,
                map<string, uint32_t>* ids, map<string, uint32_t>* bits) {
  xmlNode* cur_node = NULL;
  bool done = false;

  for (cur_node = a_node; !done && cur_node; cur_node = cur_node->next) {
    xmlChar* content = NULL;

    if (cur_node->type == XML_TEXT_NODE &&
        (content = xmlNodeGetContent(cur_node)) != NULL) {
      if (!strcmp("FuseLogicalOffset", (const char*)content)) {
        // Found a likely fuse definition section; collect it.
        done = print_fuse(a_node->parent->parent->parent, ids, bits);
      }
    }

    if (content) xmlFree(content);

    if (!done && cur_node->children) {
      done = find_fuses(cur_node->children, ids, bits);
    }
  }

  return done;
}

static
bool find_default_reg_value(xmlNodePtr a_node,
                            const string& regname, string* result) {
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
bool readXML(const string& filename,
             map<string, uint32_t>* ids,
             map<string, uint32_t>* bits,
             uint32_t* p4cl) {
  bool result = false;
  LIBXML_TEST_VERSION

  xmlDocPtr doc = xmlReadFile(filename.c_str(), NULL, 0);

  if (doc) {
    result = find_fuses(xmlDocGetRootElement(doc), ids, bits);
    string p4clStr;
    result &= find_default_reg_value(xmlDocGetRootElement(doc), "SWDP_P4_LAST_SYNC", &p4clStr);
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
bool readJSON(const string& filename,
              string* tag,
              uint32_t* keyId,
              uint32_t* p4cl,
              map<string, uint32_t>* fusemap) {
  bool result = false;
#ifdef HAVE_JSON
  ifstream ifs(filename.c_str());
  if (ifs) {

    // Touch up a bit to allow for comments.
    string s;
    while (ifs) {
      string line;
      getline(ifs, line);
      size_t nonspace = line.find_first_not_of(" \t");
      if (nonspace != string::npos &&
          line.find("//", nonspace) == nonspace) {
        continue;
      }
      s.append(line);
    }

    // Try parse.
    rapidjson::Document d;
    if (d.Parse(s.c_str()).HasParseError()) {
      fprintf(stderr, "JSON %s[%lu]: parse error\n",
              filename.c_str(), d.GetErrorOffset());
    } else {
      const rapidjson::Document::ValueType& fuses = d["fuses"];
      for (auto it = fuses.MemberBegin(); it != fuses.MemberEnd(); ++it) {
        fusemap->insert(make_pair(it->name.GetString(), it->value.GetInt()));
      }

      const rapidjson::Document::ValueType& keyid = d["keyId"];
      *keyId = keyid.GetInt();

      const rapidjson::Document::ValueType& P4cl = d["p4cl"];
      *p4cl = P4cl.GetInt();

      const rapidjson::Document::ValueType& Tag = d["tag"];
      tag->assign(Tag.GetString());

      result = true;
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

void usage(int argc, char* argv[]) {
  fprintf(stderr, "Usage: %s options\n"
          "--input=$elf-filename\n"
          "--output=output-filename\n"
          "--key=$pem-filename\n"
          "[--xml=$xml-filename] typically 'havenTop.xml'\n"
          "[--json=$json-filename] the signing manifest\n"
          "[--format=bin|hex] output file format, hex is default\n",
          argv[0]);
}

int getOptions(int argc, char* argv[]) {
  static struct option long_options[] = {
    // name, has_arg
    {"format", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"input", required_argument, NULL, 'i'},
    {"json", required_argument, NULL, 'j'},
    {"key", required_argument, NULL, 'k'},
    {"output", required_argument, NULL, 'o'},
    {"xml", required_argument, NULL, 'x'},
    {0, 0, 0, 0}
  };
  int c, option_index = 0;
  outputFormat.assign("hex");
  while ((c = getopt_long(argc, argv, "i:o:k:x:j:f:h",
                          long_options, &option_index)) != -1) {
    switch (c) {
      case 0:
        fprintf(stderr, "option %s", long_options[option_index].name);
        if (optarg) fprintf(stderr, " with arg %s", optarg);
        fprintf(stderr, "\n");
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
      case 'j':
        jsonFilename.assign(optarg);
        break;
      case 'f':
        outputFormat.assign(optarg);
        break;
      case 'h':
        usage(argc, argv);
        return 1;
      case '?':
        // getopt_long printed error
        return 1;
    }
  }
  if (inputFilename.empty() ||
      outputFilename.empty() ||
      keyFilename.empty() ||
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

  SignedHeader hdr;

  hdr.keyid = key.n0inv();
  hdr.ro_base = image.ro_base();
  hdr.ro_max = image.ro_max();
  hdr.rx_base = image.rx_base();
  hdr.rx_max = image.rx_max();

  // Parse signing manifest.
  map<string, uint32_t> fuses;
  uint32_t keyId = key.n0inv();  // default, in case no JSON.
  uint32_t json_p4cl = 0;
  string tag;

  if (!jsonFilename.empty() &&
      !readJSON(jsonFilename, &tag, &keyId, &json_p4cl, &fuses)) {
    fprintf(stderr, "Failed to read JSON from '%s'\n", jsonFilename.c_str());
    abort();
  }

  // Check keyId.
  fprintf(stderr, "keyId: %08x\n", keyId);
  if (keyId != hdr.keyid) {
    fprintf(stderr, "mismatched keyid\n");
    abort();
  }

  // Fill in tag.
  fprintf(stderr, "tag: \"%s\"\n", tag.c_str());
  strncpy((char*)(&hdr.tag), tag.c_str(), sizeof(hdr.tag));

  // List the specific fuses and values.
  fprintf(stderr, "care about %lu fuses:\n", fuses.size());
  for (auto it : fuses) {
    fprintf(stderr, "fuse '%s' should have value %u\n", it.first.c_str(), it.second);
  }

  // Parse xml.
  map<string, uint32_t> fuse_ids;
  map<string, uint32_t> fuse_bits;
  uint32_t xml_p4cl = 0;

  if (!xmlFilename.empty() &&
      !readXML(xmlFilename, &fuse_ids, &fuse_bits, &xml_p4cl)) {
    fprintf(stderr, "Failed to read XML from '%s'\n", xmlFilename.c_str());
    abort();
  }

  if (json_p4cl != xml_p4cl) {
    fprintf(stderr, "mismatching p4cl: xml %u vs. json %u\n",
            xml_p4cl, json_p4cl);
    abort();
  }

  fprintf(stderr, "found %lu fuse definitions\n", fuse_ids.size());
  assert(fuse_ids.size() < FUSE_MAX);
  for (auto it : fuse_ids) {
    fprintf(stderr, "fuse '%s' at %u, width %u\n",
            it.first.c_str(), it.second, fuse_bits[it.first]);
  }

  // Compute fuse_values array, according to manifest and xml.
  uint32_t fuse_values[FUSE_MAX];
  memset(fuse_values, FUSE_IGNORE, sizeof(fuse_values));

  for (auto x : fuses) {
    map<string, uint32_t>::const_iterator it = fuse_ids.find(x.first);
    if (it == fuse_ids.end()) {
      fprintf(stderr, "cannot find definition for fuse '%s'\n", x.first.c_str());
      abort();
    }
    uint32_t idx = it->second;
    assert(idx < FUSE_MAX);
    uint32_t mask = (1 << fuse_bits[x.first]) - 1;
    if ((x.second & mask) != x.second) {
      fprintf(stderr, "specified fuse value too large\n");
      abort();
    }
    uint32_t val = FUSE_PADDING & ~mask;
    val |= x.second;

    fuse_values[idx] = val;
    hdr.markFuse(idx);
  }

  // Print out fuse hash input.
  fprintf(stderr, "expected fuse state:\n");
  for (size_t i = 0; i < FUSE_MAX; ++i) {
    fprintf(stderr, "%08x ", fuse_values[i]);
  }
  fprintf(stderr, "\n");

  // Sign image.
  if (image.sign(key, &hdr, fuse_values)) {
    image.generate(outputFilename, outputFormat == "hex");
  } else {
    fprintf(stderr, "failed to sign\n");
  }

  return 0;
}
