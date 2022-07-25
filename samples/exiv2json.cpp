// SPDX-License-Identifier: GPL-2.0-or-later
// Sample program to print metadata in JSON format

#include <exiv2/exiv2.hpp>

#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <nlohmann/json.hpp>

#if defined(__MINGW32__) || defined(__MINGW64__)
#ifndef __MINGW__
#define __MINGW__
#endif
#endif

struct Token {
  std::string n;  // the name eg "History"
  bool a;         // name is an array eg History[]
  int i;          // index (indexed from 1) eg History[1]/stEvt:action
};
using Tokens = std::vector<Token>;

// "XMP.xmp.MP.RegionInfo/MPRI:Regions[1]/MPReg:Rectangle"
bool getToken(std::string& in, Token& token, std::set<std::string>* pNS = nullptr) {
  bool result = false;
  bool ns = false;

  token.n = "";
  token.a = false;
  token.i = 0;

  while (!result && in.length()) {
    std::string c = in.substr(0, 1);
    char C = c.at(0);
    in = in.substr(1, std::string::npos);
    if (in.length() == 0 && C != ']')
      token.n += c;
    if (C == '/' || C == '[' || C == ':' || C == '.' || C == ']' || in.length() == 0) {
      ns |= C == '/';
      token.a = C == '[';
      if (C == ']')
        token.i = std::atoi(token.n.c_str());  // encoded string first index == 1
      result = token.n.length() > 0;
    } else {
      token.n += c;
    }
  }
  if (ns && pNS)
    pNS->insert(token.n);

  return result;
}

nlohmann::json& addToTree(nlohmann::json& r1, const Token& token) {
  nlohmann::json object;
  nlohmann::json array;

  std::string key = token.n;
  size_t index = token.i - 1;  // array Eg: "History[1]" indexed from 1.  Jzon expects 0 based index.
  auto& empty = token.a ? static_cast<nlohmann::json&>(array) : static_cast<nlohmann::json&>(object);

  if (r1.is_object()) {
    if (!r1.contains(key))
      r1[key] = empty;
    return r1[key];
  }
  if (r1.is_array()) {
    while (r1.size() <= index)
      r1.push_back(empty);
    return r1[index];
  }
  return r1;
}

nlohmann::json& recursivelyBuildTree(nlohmann::json& root, Tokens& tokens, size_t k) {
  return addToTree(k == 0 ? root : recursivelyBuildTree(root, tokens, k - 1), tokens.at(k));
}

// build the json tree for this key.  return location and discover the name
nlohmann::json& objectForKey(const std::string& Key, nlohmann::json& root, std::string& name,
                         std::set<std::string>* pNS = nullptr) {
  // Parse the key
  Tokens tokens;
  Token token;
  std::string input = Key;  // Example: "XMP.xmp.MP.RegionInfo/MPRI:Regions[1]/MPReg:Rectangle"
  while (getToken(input, token, pNS))
    tokens.push_back(token);
  size_t l = tokens.size() - 1;  // leave leaf name to push()
  name = tokens.at(l).n;

  // The second token.  For example: XMP.dc is a namespace
  if (pNS && tokens.size() > 1)
    pNS->insert(tokens[1].n);
  return recursivelyBuildTree(root, tokens, l - 1);

#if 0
    // recursivelyBuildTree:
    // Go to the root.  Climb out adding objects or arrays to create the tree
    // The leaf is pushed on the top by the caller of objectForKey()
    // The recursion could be expressed by these if statements:
    if ( l == 1 ) return                               addToTree(root,tokens[0]);
    if ( l == 2 ) return                     addToTree(addToTree(root,tokens[0]),tokens[1]);
    if ( l == 3 ) return           addToTree(addToTree(addToTree(root,tokens[0]),tokens[1]),tokens[2]);
    if ( l == 4 ) return addToTree(addToTree(addToTree(addToTree(root,tokens[0]),tokens[1]),tokens[2]),tokens[3]);
    ...
#endif
}

bool isObject(std::string& value) {
  return value == std::string("type=\"Struct\"");
}

bool isArray(std::string& value) {
  return value == "type=\"Seq\"" || value == "type=\"Bag\"" || value == "type=\"Alt\"";
}

template <class T>
void push(nlohmann::json& node, const std::string& key, T i) {
#define ABORT_IF_I_EMPTY        \
  if (i->value().size() == 0) { \
    return;                     \
  }

  std::string value = i->value().toString();

  switch (i->typeId()) {
    case Exiv2::xmpText:
      if (::isObject(value)) {
        node[key] = nlohmann::json::object();
      } else if (::isArray(value)) {
        node[key] = nlohmann::json::array();
      } else {
        node[key] = value;
      }
      break;

    case Exiv2::unsignedByte:
    case Exiv2::unsignedShort:
    case Exiv2::unsignedLong:
    case Exiv2::signedByte:
    case Exiv2::signedShort:
    case Exiv2::signedLong:
      node[key] = std::atoi(value.c_str());
      break;

    case Exiv2::tiffFloat:
    case Exiv2::tiffDouble:
      node[key] = std::atof(value.c_str());
      break;

    case Exiv2::unsignedRational:
    case Exiv2::signedRational: {
      ABORT_IF_I_EMPTY
      Exiv2::Rational rat = i->value().toRational();
      node[key] = nlohmann::json::array({rat.first, rat.second});
    } break;

    case Exiv2::langAlt: {
      ABORT_IF_I_EMPTY
      nlohmann::json l;
      const auto& langs = dynamic_cast<const Exiv2::LangAltValue&>(i->value());
      for (auto&& lang : langs.value_) {
        l[lang.first] = lang.second;
      }
      node[key] = nlohmann::json::object({{"lang", l}});
    } break;

    default:
    case Exiv2::date:
    case Exiv2::time:
    case Exiv2::asciiString:
    case Exiv2::string:
    case Exiv2::comment:
    case Exiv2::undefined:
    case Exiv2::tiffIfd:
    case Exiv2::directory:
    case Exiv2::xmpAlt:
    case Exiv2::xmpBag:
    case Exiv2::xmpSeq:
      // http://dev.exiv2.org/boards/3/topics/1367#message-1373
      if (key == "UserComment") {
        size_t pos = value.find('\0');
        if (pos != std::string::npos)
          value = value.substr(0, pos);
      }
      if (key == "MakerNote")
        return;
      node[key] = value;
      break;
  }
}

void fileSystemPush(const char* path, nlohmann::json& nfs) {
  auto& fs = dynamic_cast<nlohmann::json&>(nfs);
  fs["path"] = path;
  fs["realpath"] = std::filesystem::absolute(std::filesystem::path(path)).string();

  struct stat buf = {};
  stat(path, &buf);

  fs["st_dev"] = static_cast<int>(buf.st_dev);     /* ID of device containing file    */
  fs["st_ino"] = static_cast<int>(buf.st_ino);     /* inode number                    */
  fs["st_mode"] = static_cast<int>(buf.st_mode);   /* protection                      */
  fs["st_nlink"] = static_cast<int>(buf.st_nlink); /* number of hard links            */
  fs["st_uid"] = static_cast<int>(buf.st_uid);     /* user ID of owner                */
  fs["st_gid"] = static_cast<int>(buf.st_gid);     /* group ID of owner               */
  fs["st_rdev"] = static_cast<int>(buf.st_rdev);   /* device ID (if special file)     */
  fs["st_size"] = static_cast<int>(buf.st_size);   /* total size, in bytes            */
  fs["st_atime"] = static_cast<int>(buf.st_atime); /* time of last access             */
  fs["st_mtime"] = static_cast<int>(buf.st_mtime); /* time of last modification       */
  fs["st_ctime"] = static_cast<int>(buf.st_ctime); /* time of last status change      */

#if defined(_MSC_VER) || defined(__MINGW__)
  size_t blksize = 1024;
  size_t blocks = (buf.st_size + blksize - 1) / blksize;
#else
  size_t blksize = buf.st_blksize;
  size_t blocks = buf.st_blocks;
#endif
  fs["st_blksize"] = static_cast<int>(blksize); /* blocksize for file system I/O   */
  fs["st_blocks"] = static_cast<int>(blocks);   /* number of 512B blocks allocated */
}

int main(int argc, char* const argv[]) {
  Exiv2::XmpParser::initialize();
  ::atexit(Exiv2::XmpParser::terminate);
#ifdef EXV_ENABLE_BMFF
  Exiv2::enableBMFF();
#endif

  try {
    if (argc < 2 || argc > 3) {
      std::cout << "Usage: " << argv[0] << " [-option] file" << std::endl;
      std::cout << "Option: all | exif | iptc | xmp | filesystem" << std::endl;
      return EXIT_FAILURE;
    }
    const char* path = argv[argc - 1];
    const char* opt = argc == 3 ? argv[1] : "-all";
    while (opt[0] == '-')
      opt++;  // skip past leading -'s
    char option = opt[0];

    Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(path);
    image->readMetadata();

    nlohmann::json root;

    if (option == 'f') {  // only report filesystem when requested
      const char* Fs = "FS";
      nlohmann::json fs;
      root[Fs] = fs;
      fileSystemPush(path, root[Fs]);
    }

    if (option == 'a' || option == 'e') {
      Exiv2::ExifData& exifData = image->exifData();
      for (auto i = exifData.begin(); i != exifData.end(); ++i) {
        std::string name;
        nlohmann::json& object = objectForKey(i->key(), root, name);
        push(object, name, i);
      }
    }

    if (option == 'a' || option == 'i') {
      Exiv2::IptcData& iptcData = image->iptcData();
      for (auto i = iptcData.begin(); i != iptcData.end(); ++i) {
        std::string name;
        nlohmann::json& object = objectForKey(i->key(), root, name);
        push(object, name, i);
      }
    }

#ifdef EXV_HAVE_XMP_TOOLKIT
    if (option == 'a' || option == 'x') {
      Exiv2::XmpData& xmpData = image->xmpData();
      if (!xmpData.empty()) {
        // get the xmpData and recursively parse into a Jzon Object
        std::set<std::string> namespaces;
        for (auto i = xmpData.begin(); i != xmpData.end(); ++i) {
          std::string name;
          nlohmann::json& object = objectForKey(i->key(), root, name, &namespaces);
          push(object, name, i);
        }

        // get the namespace dictionary from XMP
        Exiv2::Dictionary nsDict;
        Exiv2::XmpProperties::registeredNamespaces(nsDict);

        // create and populate a nlohmann::json for the namespaces
        nlohmann::json xmlns;
        for (auto&& ns : namespaces) {
          xmlns[ns] = nsDict[ns];
        }

        // add xmlns as Xmp.xmlns
        root["Xmp"]["xmlns"] = xmlns;
      }
    }
#endif

    std::cout << root.dump(1, '\t') << std::endl;
    return EXIT_SUCCESS;
  }

  catch (Exiv2::Error& e) {
    std::cout << "Caught Exiv2 exception '" << e.what() << "'\n";
    return EXIT_FAILURE;
  }
}
