#include "perf.h"
#include <cstdlib>
#include <format>
#include <iostream>
// #include <nlohmann/json.hpp>
#include "json.hpp"
#include "tinyxml2.h"

using nlohmann::json;
using namespace tinyxml2;

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";  // No content

    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

uint32_t ConvertToIntArray(const char* str, int32_t intArray[], uint32_t size) {
    uint32_t i = 0;
    char* endPtr;

    if ((NULL == str) || (NULL == intArray)) {
        return i;
    }

    char* pos = NULL;
    char* token = strtok_r(const_cast<char*>(str), ",", &pos);

    while (token != NULL && i < size) {
        std::string trimmedToken = trim(token);

        if (!trimmedToken.empty()) {
            intArray[i] = strtol(trimmedToken.c_str(), &endPtr, 0);
            if (*endPtr != '\0') {
                std::cout << "Invalid value found in strtol: " << trimmedToken << std::endl;
            }
            i++;
        }

        token = strtok_r(NULL, ",", &pos);
    }

    return i;
}

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2>& pair) const {
        auto h1 = std::hash<T1>{}(pair.first);
        auto h2 = std::hash<T2>{}(pair.second);
        return h1 ^ h2;  // Combine the two hashes
    }
};

static const std::unordered_map<HintKey, std::string, HintKeyHash> kHintToPowerHint = {
        {{0x00001080, 1, 120, "volcano"}, "INTERACTION"},  // VENDOR_HINT_SCROLL_BOOST
        {{0x00001081, 10, -1, "volcano"}, "LAUNCH"},       // VENDOR_HINT_FIRST_LAUNCH_BOOST
        // { { another_id, another_type, another_fps }, "LAUNCH" },
};

int clusterToCpuIndex(int cluster) {
    switch (cluster) {
        case 0:
            return 4;  // big
        case 1:
            return 0;  // little
        case 2:
            return 7;  // prime
        default:
            return 99;
    }
}

std::string getPowerHintName(const PerfBoost& boost) {
    HintKey key{boost.id, boost.type, boost.fps, boost.target};
    auto it = kHintToPowerHint.find(key);
    if (it == kHintToPowerHint.end()) {
        return {};  // or some default, or throw/ignore
    }
    return it->second;
}

std::string makeNodeName(const Resource& res, const ResourceConfig& rc) {
    const int cluster = res.cluster;

    if (rc.node == "/sys/kernel/msm_performance/parameters/cpu_min_freq") {
        if (cluster == 0) return "CPUBoostMinFreqBig";
        if (cluster == 1) return "CPUBoostMinFreqLittle";
        if (cluster == 2) return "CPUBoostMinFreqPrime";
    }
    if (rc.node == "/sys/kernel/msm_performance/parameters/cpu_max_freq") {
        if (cluster == 0) return "CPUBoostMaxFreqBig";
        if (cluster == 1) return "CPUBoostMaxFreqLittle";
        if (cluster == 2) return "CPUBoostMaxFreqPrime";
    }

    if (rc.node == "/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_high_freq") {
        if (cluster == 0) return "WaltAdaptiveHighFreqBig";
        if (cluster == 1) return "WaltAdaptiveHighFreqLittle";
        if (cluster == 2) return "WaltAdaptiveHighFreqPrime";
    }
    if (rc.node == "/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_low_freq") {
        if (cluster == 0) return "WaltAdaptiveLowFreqBig";
        if (cluster == 1) return "WaltAdaptiveLowFreqLittle";
        if (cluster == 2) return "WaltAdaptiveLowFreqPrime";
    }

    // Fallback: derive name from path (replace / with _ etc.)
    std::string name = rc.node;
    for (auto& ch : name) {
        if (ch == '/')
            ch = '_';
        else if (ch == '-')
            ch = '_';
    }
    return name;
}

std::string makeNodePath(const Resource& res, const ResourceConfig& rc) {
    const int cluster = res.cluster;

    if (rc.node == "/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_high_freq") {
        return "/sys/devices/system/cpu/cpufreq/policy" +
               std::to_string(clusterToCpuIndex(cluster)) + "/walt/adaptive_high_freq";
    }
    if (rc.node == "/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_low_freq") {
        return "/sys/devices/system/cpu/cpufreq/policy" +
               std::to_string(clusterToCpuIndex(cluster)) + "/walt/adaptive_low_freq";
    }

    return rc.node;
}

std::string makeValueString(const Resource& res, const ResourceConfig& rc) {
    int v = res.value;
    const int cluster = res.cluster;

    if (rc.node == "/sys/kernel/msm_performance/parameters/cpu_min_freq" ||
        rc.node == "/sys/kernel/msm_performance/parameters/cpu_max_freq") {
        return std::to_string(clusterToCpuIndex(cluster)) + ":" + std::to_string(v);
    }

    return std::to_string(v);
}

int parsePerfBoostsConfig(XMLElement* configs, std::vector<PerfBoost>* perfBoosts) {
    XMLElement* configElement = configs->FirstChildElement(BOOSTS_CONFIGS_XML_ELEM_CONFIG_TAG);
    while (configElement) {
        PerfBoost boost;

        const char* id = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_ID_TAG);
        const char* type = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_TYPE_TAG);
        const char* enable = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_ENABLE_TAG);
        const char* timeout = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_TIMEOUT_TAG);
        const char* target = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_TARGET_TAG);
        const char* kernel = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_KERNEL_TAG);
        const char* fpsStr = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_FPS_TAG);

        boost.id = id ? strtol(id, nullptr, 16) : -1;
        boost.type = type ? atoi(type) : -1;
        boost.enable = (enable && strcmp(enable, "true") == 0);
        boost.timeout = timeout ? atoi(timeout) : -1;
        boost.target = target ? target : "UNSET";
        boost.kernel = kernel ? kernel : "UNSET";
        boost.fps = fpsStr ? atoi(fpsStr) : -1;

        const char* resourcesStr = configElement->Attribute("Resources");
        if (resourcesStr) {
            int32_t mConfigTable[MAX_OPCODE_VALUE_TABLE_SIZE];
            uint32_t numParsedValues = ConvertToIntArray(
                    resourcesStr, mConfigTable, sizeof(mConfigTable) / sizeof(mConfigTable[0]));

            for (uint32_t j = 0; j < numParsedValues; j += 2) {
                if (j + 1 < numParsedValues) {  // Ensure there are pairs available.
                    Resource resource = Resource(mConfigTable[j], mConfigTable[j + 1]);
                    boost.resources.push_back(resource);
                } else {
                    std::cout << "Missing value for opcode: " << mConfigTable[j] << std::endl;
                }
            }
        }

        perfBoosts->push_back(boost);
        configElement = configElement->NextSiblingElement(BOOSTS_CONFIGS_XML_ELEM_CONFIG_TAG);
    }

    return 0;
}

std::vector<int> toIntVector(const std::string& s) {
    std::vector<int> result;
    std::stringstream ss(s);
    std::string item;

    while (std::getline(ss, item, ',')) {
        result.push_back(std::stoi(item));
    }

    return result;
}

int main(int argc, char* argv[]) {
    XMLDocument doc;
    XMLElement* element;
    std::vector<PerfBoost> perfBoosts;
    std::unordered_map<std::pair<int, int>, ResourceConfig, pair_hash> resourceMap;
    std::vector<TargetInfo> targets;
    if (argc != 6) {
        std::cout << "usage: ./perf perfboostsconfig.xml powerhint.xml commonresourceconfigs.xml "
                     "targetresourceconfigs.xml targetconfig.xml"
                  << std::endl;
        return 0;
    }

    //////////////////////////
    // perfboostsconfig.xml //
    //////////////////////////
    doc.LoadFile(argv[1]);
    if (doc.Error()) {
        std::cerr << "Error loading file: " << doc.ErrorIDToName(doc.ErrorID()) << std::endl;
        return -1;
    }

    element = doc.FirstChildElement(BOOSTS_CONFIGS_XML_ROOT);
    if (!element) {
        std::cerr << "No BoostConfigs element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement(BOOSTS_CONFIGS_XML_CHILD_CONFIG);
    if (!element) {
        std::cerr << "No PerfBoost element found." << std::endl;
        return -1;
    }

    parsePerfBoostsConfig(element, &perfBoosts);

    ///////////////////
    // powerhint.xml //
    ///////////////////
    doc.LoadFile(argv[2]);
    if (doc.Error()) {
        std::cerr << "Error loading file: " << doc.ErrorIDToName(doc.ErrorID()) << std::endl;
        return -1;
    }

    element = doc.FirstChildElement("HintConfigs");
    if (!element) {
        std::cerr << "No HintConfigs element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement("Powerhint");
    if (!element) {
        std::cerr << "No Powerhint element found." << std::endl;
        return -1;
    }

    parsePerfBoostsConfig(element, &perfBoosts);

    ///////////////////////////////
    // commonresourceconfigs.xml //
    ///////////////////////////////
    doc.LoadFile(argv[3]);
    if (doc.Error()) {
        std::cerr << "Error loading file: " << doc.ErrorIDToName(doc.ErrorID()) << std::endl;
        return -1;
    }

    element = doc.FirstChildElement("ResourceConfigs");
    if (!element) {
        std::cerr << "No ResourceConfigs element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement("PerfResources");
    if (!element) {
        std::cerr << "No PerfResources element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement("Major");
    int currentMajor = -1;
    while (element) {
        if (strcmp(element->Name(), "Major") == 0) {
            const char* opcodeValueStr = element->Attribute("OpcodeValue");
            if (opcodeValueStr) {
                currentMajor = static_cast<int>(std::stoul(opcodeValueStr, nullptr, 16));
            }
        } else if (strcmp(element->Name(), "Minor") == 0) {
            const char* opcodeValueStr = element->Attribute("OpcodeValue");
            if (opcodeValueStr) {
                int minorOpcode = static_cast<int>(std::stoul(opcodeValueStr, nullptr, 16));

                const char* node = element->Attribute("Node");
                const char* supported = element->Attribute("Supported");
                if (node) {
                    auto key = std::make_pair(currentMajor, minorOpcode);
                    resourceMap[key] = {currentMajor, minorOpcode,
                                        supported != nullptr ? strcmp(supported, "no") != 0 : true,
                                        node};
                }
            }
        } else {
            std::cout << "ERROR, unknown element: " << element->Name() << std::endl;
            return 0;
        }
        element = element->NextSiblingElement();
    }

    ///////////////////////////////
    // targetresourceconfigs.xml //
    ///////////////////////////////
    doc.LoadFile(argv[4]);
    if (doc.Error()) {
        std::cerr << "Error loading file: " << doc.ErrorIDToName(doc.ErrorID()) << std::endl;
        return -1;
    }

    element = doc.FirstChildElement("ResourceConfigs");
    if (!element) {
        std::cerr << "No ResourceConfigs element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement("PerfResources");
    if (!element) {
        std::cerr << "No PerfResources element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement("Config");
    while (element) {
        const char* majorOpcodeStr = element->Attribute("MajorValue");
        const char* minorOpcodeStr = element->Attribute("MinorValue");
        const char* node = element->Attribute("Node");
        const char* supportedStr = element->Attribute("Supported");

        int majorOpcode = static_cast<int>(std::stoul(majorOpcodeStr, nullptr, 16));
        int minorOpcode = static_cast<int>(std::stoul(minorOpcodeStr, nullptr, 16));

        auto key = std::make_pair(majorOpcode, minorOpcode);
        resourceMap[key] = {majorOpcode, minorOpcode,
                            (supportedStr != nullptr ? strcmp(supportedStr, "no") != 0 : true),
                            node != nullptr ? node : "NO PATH"};
        element = element->NextSiblingElement("Config");
    }

    //////////////////////
    // targetconfig.xml //
    //////////////////////
    doc.LoadFile(argv[5]);
    if (doc.Error()) {
        std::cerr << "Error loading file: " << doc.ErrorIDToName(doc.ErrorID()) << std::endl;
        return -1;
    }

    element = doc.FirstChildElement("TargetConfig");
    if (!element) {
        std::cerr << "No TargetConfig element found." << std::endl;
        return -1;
    }

    element = element->FirstChildElement();
    if (!element) {
        std::cerr << "No Config[0-09] element found." << std::endl;
        return -1;
    }
    while (element) {
        XMLElement* infoElement = element->FirstChildElement();
        TargetInfo target;
        while (infoElement) {
            if (strcmp(infoElement->Name(), "TargetInfo") == 0) {
                target.target = infoElement->Attribute("Target");
                target.numClusters = atoi(infoElement->Attribute("NumClusters"));
                target.socIds = toIntVector(infoElement->Attribute("SocIds"));
                target.synCore = atoi(infoElement->Attribute("SynCore"));
                target.coreCtlCpu = atoi(infoElement->Attribute("CoreCtlCpu"));
                target.minCoreOnline = atoi(infoElement->Attribute("MinCoreOnline"));
                target.cpufreqGov = atoi(infoElement->Attribute("CpufreqGov"));
            } else if (strcmp(infoElement->Name(), "ClustersInfo") == 0) {
                target.clusters.push_back(ClustersInfo(infoElement->Attribute("Id"),
                                                       infoElement->Attribute("NumCores"),
                                                       infoElement->Attribute("Type")));
            }
            infoElement = infoElement->NextSiblingElement();
        }
        targets.push_back(target);
        element = element->NextSiblingElement();
    }

    ////////////////////////////////
    // Print parsed config /////////
    ////////////////////////////////
    for (PerfBoost boost : perfBoosts) {
        auto it = hintMap.find(boost.id);
        std::cout << "Config ID: " << std::format("0x{:x}", boost.id) << "("
                  << ((it != hintMap.end()) ? it->second : "UNKNOWN HINT") << ")" << std::endl
                  << "Type: " << boost.type << std::endl
                  << "Enable: " << (boost.enable ? "true" : "false") << std::endl
                  << "Timeout: " << boost.timeout << std::endl
                  << "Target: " << boost.target << std::endl
                  << "Kernel: " << boost.kernel << std::endl
                  << "FPS: " << boost.fps << std::endl;

        for (const auto& res : boost.resources) {
            auto key = std::make_pair(res.major, res.minor);
            std::cout << std::format("(0x{:x}, 0x{:x})", resourceMap[key].major,
                                     resourceMap[key].minor)
                      << " -> value: " << res.value << " on \"" << resourceMap[key].node << "\""
                      << ", cluster: "
                      << res.cluster
                      //   << " on core: " << res.getCore()
                      << (resourceMap[key].supported ? "" : " UNSUPPORTED") << std::endl;
        }
        std::cout << std::endl;
    }

    ///////////////////////////////
    // powerhint.json generation //
    ///////////////////////////////
    json nodesJson = json::array();
    json actionsJson = json::array();

    std::map<std::string, NodeInfo> nodeTable;  // name -> NodeInfo

    for (const PerfBoost& boost : perfBoosts) {
        if (!boost.enable) {
            continue;
        }

        std::string powerHint = getPowerHintName(boost);
        if (powerHint.empty()) {
            // Unknown (id, type, fps) -> skip this boost
            continue;
        }

        const int duration = boost.timeout;

        for (const auto& res : boost.resources) {
            auto key = std::make_pair(res.major, res.minor);
            auto rcIt = resourceMap.find(key);
            if (rcIt == resourceMap.end()) {
                continue;
            }

            const ResourceConfig& rc = rcIt->second;
            if (!rc.supported) {
                continue;
            }

            std::string nodeName = makeNodeName(res, rc);
            std::string nodePath = makeNodePath(res, rc);
            std::string valueStr = makeValueString(res, rc);

            // Update / create NodeInfo
            NodeInfo& n = nodeTable[nodeName];
            if (n.name.empty()) {
                n.name = nodeName;
                n.path = nodePath;
                n.defaultIndex = 0;
                // TODO not sure about this?
                n.resetOnInit = true;
            }
            n.values.insert(valueStr);

            json action;
            action["PowerHint"] = powerHint;
            action["Node"] = nodeName;
            action["Duration"] = duration;
            action["Value"] = valueStr;
            actionsJson.push_back(action);
        }
    }

    for (const auto& [name, info] : nodeTable) {
        json node;
        node["Name"] = info.name;
        node["Path"] = info.path;

        json valuesJson = json::array();
        for (const auto& v : info.values) {
            valuesJson.push_back(v);
        }
        node["Values"] = valuesJson;

        node["DefaultIndex"] = info.defaultIndex;
        node["ResetOnInit"] = info.resetOnInit;

        nodesJson.push_back(node);
    }

    json jsonRoot;
    jsonRoot["Nodes"] = nodesJson;
    jsonRoot["Actions"] = actionsJson;

    // Pretty-print
    std::cout << jsonRoot.dump(2) << std::endl;

    return 0;
}
