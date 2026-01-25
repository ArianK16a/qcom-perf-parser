#include "perf.h"
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include "cpu_freq_utils.h"
#include "json.hpp"
#include "tinyxml2.h"

using json = nlohmann::ordered_json;
using namespace tinyxml2;

std::string readNodeDefaultValueViaAdb(const std::string& path) {
    std::string cmd = "adb shell \"cat " + path + "\"";

    std::array<char, 256> buffer;
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);

    if (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

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
        // {{0x00001081, 10, -1, "volcano"}, "LAUNCH"},       // VENDOR_HINT_FIRST_LAUNCH_BOOST
        // {{0x00001337, -1, -1, "volcano"}, "CAMERA"},
        // { { another_id, another_type, another_fps }, "LAUNCH" },
};

int clusterToCpuIndex(const TargetInfo& target, int clusterId) {
    int cpuIndex = 0;

    for (const auto& c : target.clusters) {
        if (c.id == clusterId) {
            return cpuIndex;
        }
        cpuIndex += c.numCores;
    }

    return -1;
}

std::string getPowerHintName(const PerfBoost& boost) {
    HintKey key{boost.id, boost.type, boost.fps, boost.target};
    auto it = kHintToPowerHint.find(key);
    if (it == kHintToPowerHint.end()) {
        return {};  // or some default, or throw/ignore
    }
    return it->second;
}

const std::unordered_set<std::string> kNoDefaultNodes = {
        "/dev/cpu_dma_latency",
        "/sys/kernel/msm_performance/parameters/cpu_min_freq",
        "/sys/kernel/msm_performance/parameters/cpu_max_freq",
};

const std::unordered_set<std::string> kHoldFdNodes = {
        "/dev/cpu_dma_latency",
};

const std::unordered_set<std::string> kWriteOnlyNodes = {
        "/sys/kernel/msm_performance/parameters/cpu_min_freq",
        "/sys/kernel/msm_performance/parameters/cpu_max_freq",
};

// Fixed names (not depending on cluster)
const std::unordered_map<std::string, std::string> kFixedNodeNames = {
        {"/sys/kernel/msm_performance/parameters/cpu_min_freq", "MSMPerfMinFreq"},
        {"/sys/kernel/msm_performance/parameters/cpu_max_freq", "MSMPerfMaxFreq"},
        {"/dev/cpu_dma_latency", "PMQoSCpuDmaLatency"},
};

// Cluster-dependent names
const std::unordered_map<std::string, std::array<const char*, 3>> kClusterNodeNames = {
        {"/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_high_freq",
         {"WaltAdaptiveHighFreqBig", "WaltAdaptiveHighFreqLittle", "WaltAdaptiveHighFreqPrime"}},
        {"/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_low_freq",
         {"WaltAdaptiveLowFreqBig", "WaltAdaptiveLowFreqLittle", "WaltAdaptiveLowFreqPrime"}},
};

std::string makeNodeName(const Resource& res, const ResourceConfig& rc, const TargetInfo& target) {
    const auto& node = rc.node;
    const int cluster = res.cluster;

    // Fixed-name nodes
    if (auto it = kFixedNodeNames.find(node); it != kFixedNodeNames.end()) {
        return it->second;
    }

    // Cluster-dependent nodes
    if (auto it = kClusterNodeNames.find(node); it != kClusterNodeNames.end()) {
        const auto& names = it->second;
        if (cluster >= 0 && cluster < static_cast<int>(names.size()) && names[cluster] != nullptr) {
            return names[cluster];
        }
    }

    // Fallback: derive name from path (replace / and - with _)
    std::string name = rc.node;
    for (auto& ch : name) {
        if (ch == '/')
            ch = '_';
        else if (ch == '-')
            ch = '_';
    }
    return name;
}

std::string makeNodePath(const Resource& res, const ResourceConfig& rc, const TargetInfo& target) {
    const int cluster = res.cluster;

    if (rc.node == "/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_high_freq") {
        return "/sys/devices/system/cpu/cpufreq/policy" +
               std::to_string(clusterToCpuIndex(target, cluster)) + "/walt/adaptive_high_freq";
    }
    if (rc.node == "/sys/devices/system/cpu/cpufreq/policy0/walt/adaptive_low_freq") {
        return "/sys/devices/system/cpu/cpufreq/policy" +
               std::to_string(clusterToCpuIndex(target, cluster)) + "/walt/adaptive_low_freq";
    }

    return rc.node;
}

std::map<int, int> parseMsmPerfValueString(const std::string& s) {
    std::map<int, int> result;
    std::istringstream iss(s);
    std::string token;

    while (iss >> token) {
        auto pos = token.find(':');
        if (pos == std::string::npos) {
            continue;  // or log error
        }

        int cpu = std::stoi(token.substr(0, pos));
        std::string val = token.substr(pos + 1);
        result[cpu] = std::stoi(val);
    }

    return result;
}

std::string buildMsmPerfValueString(const std::map<int, int>& values) {
    std::string result;
    bool first = true;

    for (const auto& [cpu, val] : values) {
        if (!first) {
            result += ' ';
        }
        first = false;

        result += std::to_string(cpu);
        result += ':';
        result += std::to_string(val);
    }

    return result;
}

#define FREQ_MULTIPLICATION_FACTOR 1000ul
std::string makeMsmPerfValueString(const TargetInfo& target, int clusterId, int value,
                                   std::string previousValue, bool forceValue = false) {
    int startCpu = clusterToCpuIndex(target, clusterId);

    auto it = std::find_if(target.clusters.begin(), target.clusters.end(),
                           [&](const ClustersInfo& c) { return c.id == clusterId; });

    if (it == target.clusters.end()) {
        return "ERROR";
    }

    std::map<int, int> valueMap = parseMsmPerfValueString(previousValue);
    int requestedFrequency =
            forceValue ? value
                       : find_closest_freq_for_cpu(startCpu, (value * FREQ_MULTIPLICATION_FACTOR));
    for (int i = 0; i < it->numCores; ++i) {
        // oss << (startCpu + i) << ':' << requestedFrequency;
        valueMap.insert({startCpu + i, requestedFrequency});
    }

    return buildMsmPerfValueString(valueMap);
}

std::string makeValueString(const Resource& res, const ResourceConfig& rc, const TargetInfo& target,
                            std::string previousValue) {
    int v = res.value;
    const ClusterType cluster = (ClusterType)res.cluster;

    if (rc.node == "/sys/kernel/msm_performance/parameters/cpu_min_freq" ||
        rc.node == "/sys/kernel/msm_performance/parameters/cpu_max_freq") {
        return makeMsmPerfValueString(target, cluster, v, previousValue);
    }

    return std::to_string(v);
}

std::string makeDefaultValueString(const Resource& res, const ResourceConfig& rc,
                                   const TargetInfo& target) {
    const ClusterType cluster = (ClusterType)res.cluster;

    if (rc.node == "/sys/kernel/msm_performance/parameters/cpu_min_freq" ||
        rc.node == "/sys/kernel/msm_performance/parameters/cpu_max_freq") {
        return buildMsmPerfValueString(std::map<int, int>{
                {0, 0},
                {1, 0},
                {2, 0},
                {3, 0},
                {4, 0},
                {5, 0},
                {6, 0},
                {7, 0},
        });
    }
    return readNodeDefaultValueViaAdb(makeNodePath(res, rc, target));
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
        boost.timeout = timeout ? atoi(timeout) : 0;
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
        auto it = std::find_if(targets.begin(), targets.end(),
                               [&](const TargetInfo& t) { return t.target == boost.target; });
        if (it == targets.end()) {
            std::cout << "missing targetinfo for " << boost.target << std::endl;
            return -1;
        }
        TargetInfo& target = *it;

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

            std::string nodeName = makeNodeName(res, rc, target);

            // libperfmgr doesn't support multiple values for one node within one action
            auto it = std::find_if(actionsJson.begin(), actionsJson.end(), [&](const json& elem) {
                return elem.value("Node", std::string{}) == nodeName;
            });

            if (it != actionsJson.end()) {
                json& action = *it;
                action["Value"] = makeValueString(res, rc, target, action["Value"]);
            } else {
                json action;
                action["PowerHint"] = powerHint;
                action["Node"] = nodeName;
                action["Value"] = makeValueString(res, rc, target, "");
                action["Duration"] = duration;
                actionsJson.push_back(action);
            }

            // prepare the node information for later except for value which could still change in
            // this loop when multiple resources adjust the same node
            NodeInfo& n = nodeTable[nodeName];
            if (n.name.empty()) {
                bool hasDefault = rc.node.rfind("SPECIAL_NODE", 0) != 0 &&
                                  kNoDefaultNodes.find(rc.node) == kNoDefaultNodes.end();

                n.name = nodeName;
                n.path = makeNodePath(res, rc, target);
                n.hasDefault = hasDefault;
                n.defaultValue = hasDefault ? makeDefaultValueString(res, rc, target) : "";
                n.holdFd = kHoldFdNodes.find(rc.node) != kHoldFdNodes.end();
                n.writeOnly = kWriteOnlyNodes.find(rc.node) != kWriteOnlyNodes.end();
            }
        }
        // Now the final value for the node during this boost is determined
        for (const auto& action : actionsJson) {
            NodeInfo& n = nodeTable[action["Node"]];
            n.values.insert(action["Value"]);
        }
    }

    for (const auto& [name, info] : nodeTable) {
        json node;
        node["Name"] = info.name;
        node["Path"] = info.path;

        json valuesJson = json::array();
        if (info.hasDefault) {
            valuesJson.push_back(info.defaultValue);
        }
        for (const auto& v : info.values) {
            valuesJson.push_back(v);
        }
        node["Values"] = valuesJson;
        if (info.hasDefault) {
            node["DefaultIndex"] = 0;
            node["ResetOnInit"] = true;
        }
        if (info.holdFd) {
            node["HoldFd"] = true;
        }
        if (info.writeOnly) {
            node["WriteOnly"] = true;
        }

        nodesJson.push_back(node);
    }

    json jsonRoot;
    jsonRoot["Nodes"] = nodesJson;
    jsonRoot["Actions"] = actionsJson;

    // Pretty-print
    std::cout << jsonRoot.dump(2) << std::endl;

    return 0;
}
