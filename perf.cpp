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

std::string getPowerHintName(const Config& config) {
    HintKey key{config.id, config.type, config.fps};
    auto it = kHintToPowerHint.find(key);
    if (it == kHintToPowerHint.end()) {
        return {};  // or some default, or throw/ignore
    }
    return it->second;
}

std::string makeNodeName(const Resource& res, const ResourceConfig& rc) {
    const int cluster = res.getCluster();

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
    const int cluster = res.getCluster();

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
    const int cluster = res.getCluster();

    if (rc.node == "/sys/kernel/msm_performance/parameters/cpu_min_freq" ||
        rc.node == "/sys/kernel/msm_performance/parameters/cpu_max_freq") {
        return std::to_string(clusterToCpuIndex(cluster)) + ":" + std::to_string(v);
    }

    return std::to_string(v);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cout << "usage: ./perf perfboostsconfig.xml commonresourceconfigs.xml "
                     "targetresourceconfigs.xml"
                  << std::endl;
        return 0;
    }

    XMLDocument doc;
    doc.LoadFile(argv[1]);

    if (doc.Error()) {
        std::cerr << "Error loading file: " << doc.ErrorIDToName(doc.ErrorID()) << std::endl;
        return -1;
    }

    XMLElement* root = doc.FirstChildElement(BOOSTS_CONFIGS_XML_ROOT);
    if (!root) {
        std::cerr << "No root element found." << std::endl;
        return -1;
    }

    XMLElement* perfBoost = root->FirstChildElement(BOOSTS_CONFIGS_XML_CHILD_CONFIG);

    if (!perfBoost) {
        std::cerr << "No PerfBoost element found." << std::endl;
        return -1;
    }

    PerfBoost perfBoostStruct;

    XMLElement* configElement = perfBoost->FirstChildElement(BOOSTS_CONFIGS_XML_ELEM_CONFIG_TAG);
    while (configElement) {
        Config config;

        const char* id = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_ID_TAG);
        const char* type = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_TYPE_TAG);
        const char* enable = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_ENABLE_TAG);
        const char* timeout = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_TIMEOUT_TAG);
        const char* target = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_TARGET_TAG);
        const char* kernel = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_KERNEL_TAG);
        const char* fpsStr = configElement->Attribute(BOOSTS_CONFIGS_XML_ELEM_FPS_TAG);

        config.id = id ? strtol(id, nullptr, 16) : -1;
        config.type = type ? atoi(type) : -1;
        config.enable = (enable && strcmp(enable, "true") == 0);
        config.timeout = timeout ? atoi(timeout) : -1;
        config.target = target ? target : "UNSET";
        config.kernel = kernel ? kernel : "UNSET";
        config.fps = fpsStr ? atoi(fpsStr) : -1;

        const char* resourcesStr = configElement->Attribute("Resources");
        if (resourcesStr) {
            int32_t mConfigTable[MAX_OPCODE_VALUE_TABLE_SIZE];
            uint32_t numParsedValues = ConvertToIntArray(
                    resourcesStr, mConfigTable, sizeof(mConfigTable) / sizeof(mConfigTable[0]));

            for (uint32_t j = 0; j < numParsedValues; j += 2) {
                if (j + 1 < numParsedValues) {  // Ensure there are pairs available.
                    Resource resource{mConfigTable[j], mConfigTable[j + 1]};
                    config.resources.push_back(resource);
                } else {
                    std::cout << "Missing value for opcode: " << mConfigTable[j] << std::endl;
                }
            }
        }

        perfBoostStruct.configs.push_back(config);
        configElement = configElement->NextSiblingElement(BOOSTS_CONFIGS_XML_ELEM_CONFIG_TAG);
    }

    ///////////////////////////////
    // commonresourceconfigs.xml //
    ///////////////////////////////
    if (doc.LoadFile(argv[2]) != XML_SUCCESS) {
        std::cerr << "Error loading file!" << std::endl;
        return -1;
    }

    std::unordered_map<std::pair<int, int>, ResourceConfig, pair_hash> resourceMap;

    root = doc.FirstChildElement("ResourceConfigs");
    if (!root) {
        std::cerr << "No root element found." << std::endl;
        return -1;
    }

    XMLElement* perfResources = root->FirstChildElement("PerfResources");

    if (!perfResources) {
        std::cerr << "No PerfResources element found." << std::endl;
        return -1;
    }

    XMLElement* currentElement = perfResources->FirstChildElement("Major");
    int currentMajor = -1;
    while (currentElement) {
        if (strcmp(currentElement->Name(), "Major") == 0) {
            const char* opcodeValueStr = currentElement->Attribute("OpcodeValue");
            if (opcodeValueStr) {
                currentMajor = static_cast<int>(std::stoul(opcodeValueStr, nullptr, 16));
            }
        } else if (strcmp(currentElement->Name(), "Minor") == 0) {
            const char* opcodeValueStr = currentElement->Attribute("OpcodeValue");
            if (opcodeValueStr) {
                int minorOpcode = static_cast<int>(std::stoul(opcodeValueStr, nullptr, 16));

                const char* node = currentElement->Attribute("Node");
                const char* supported = currentElement->Attribute("Supported");
                if (node) {
                    auto key = std::make_pair(currentMajor, minorOpcode);
                    resourceMap[key] = {currentMajor, minorOpcode,
                                        supported != nullptr ? strcmp(supported, "no") != 0 : true,
                                        node};
                }
            }
        } else {
            std::cout << "ERROR, unknown element: " << currentElement->Name() << std::endl;
            return 0;
        }
        currentElement = currentElement->NextSiblingElement();
    }

    ///////////////////////////////
    // targetresourceconfigs.xml //
    ///////////////////////////////
    if (doc.LoadFile(argv[3]) != XML_SUCCESS) {
        std::cerr << "Error loading file!" << std::endl;
        return -1;
    }

    root = doc.FirstChildElement("ResourceConfigs");
    if (!root) {
        std::cerr << "No root element found." << std::endl;
        return -1;
    }

    XMLElement* targetPerfResources = root->FirstChildElement("PerfResources");

    if (!targetPerfResources) {
        std::cerr << "No PerfResources element found." << std::endl;
        return -1;
    }

    currentElement = targetPerfResources->FirstChildElement("Config");
    while (currentElement) {
        const char* majorOpcodeStr = currentElement->Attribute("MajorValue");
        const char* minorOpcodeStr = currentElement->Attribute("MinorValue");
        const char* node = currentElement->Attribute("Node");
        const char* supportedStr = currentElement->Attribute("Supported");

        int majorOpcode = static_cast<int>(std::stoul(majorOpcodeStr, nullptr, 16));
        int minorOpcode = static_cast<int>(std::stoul(minorOpcodeStr, nullptr, 16));

        auto key = std::make_pair(majorOpcode, minorOpcode);
        resourceMap[key] = {majorOpcode, minorOpcode,
                            (supportedStr != nullptr ? strcmp(supportedStr, "no") != 0 : true),
                            node != nullptr ? node : "NO PATH"};
        currentElement = currentElement->NextSiblingElement("Config");
    }

    for (Config config : perfBoostStruct.configs) {
        auto it = hintMap.find(config.id);
        std::cout << "Config ID: " << std::format("0x{:x}", config.id) << "("
                  << ((it != hintMap.end()) ? it->second : "UNKNOWN HINT") << ")" << std::endl
                  << "Type: " << config.type << std::endl
                  << "Enable: " << (config.enable ? "true" : "false") << std::endl
                  << "Timeout: " << config.timeout << std::endl
                  << "Target: " << config.target << std::endl
                  << "Kernel: " << config.kernel << std::endl
                  << "FPS: " << config.fps << std::endl;

        for (const auto& res : config.resources) {
            auto key = std::make_pair(res.getMajor(), res.getMinor());
            std::cout << std::format("(0x{:x}, 0x{:x})", resourceMap[key].major,
                                     resourceMap[key].minor)
                      << " -> value: " << res.value << " on \"" << resourceMap[key].node << "\""
                      << ", cluster: "
                      << res.getCluster()
                      //   << " on core: " << res.getCore()
                      << (resourceMap[key].supported ? "" : " UNSUPPORTED") << std::endl;
        }
        std::cout << std::endl;
    }

    json nodesJson = json::array();
    json actionsJson = json::array();

    std::map<std::string, NodeInfo> nodeTable;  // name -> NodeInfo

    for (const Config& config : perfBoostStruct.configs) {
        if (!config.enable) {
            continue;
        }

        std::string powerHint = getPowerHintName(config);
        if (powerHint.empty()) {
            // Unknown (id, type, fps) -> skip this config
            continue;
        }

        const int duration = config.timeout;  // Duration = timeout

        for (const auto& res : config.resources) {
            auto key = std::make_pair(res.getMajor(), res.getMinor());
            auto rcIt = resourceMap.find(key);
            if (rcIt == resourceMap.end()) {
                continue;  // unknown resource, skip
            }

            const ResourceConfig& rc = rcIt->second;
            if (!rc.supported) {
                continue;  // skip unsupported
            }

            std::string nodeName = makeNodeName(res, rc);
            std::string nodePath = makeNodePath(res, rc);
            std::string valueStr = makeValueString(res, rc);

            // Update / create NodeInfo
            NodeInfo& n = nodeTable[nodeName];
            if (n.name.empty()) {
                n.name = nodeName;
                n.path = nodePath;
                n.defaultIndex = 0;    // or customize
                n.resetOnInit = true;  // or customize
            } else {
                // Sanity check: same name should not change path
                if (n.path != nodePath) {
                    std::cerr << "Warning: node " << nodeName
                              << " has conflicting paths: " << n.path << " vs " << nodePath
                              << std::endl;
                }
            }
            n.values.insert(valueStr);

            // Create Action entry
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

ResourceType Resource::getType() const {
    // TODO how to detect other types?
    return SINGLE_NODE;
}

int Resource::getMajor() const {
    return (this->opcode & EXTRACT_MAJOR_TYPE) >> SHIFT_BIT_MAJOR_TYPE;
}

int Resource::getMinor() const {
    return (this->opcode & EXTRACT_MINOR_TYPE) >> SHIFT_BIT_MINOR_TYPE;
}

int Resource::getCluster() const {
    return (this->opcode & EXTRACT_CLUSTER) >> SHIFT_BIT_CLUSTER;
}

int Resource::getCore() const {
    return (this->opcode & EXTRACT_CORE) >> SHIFT_BIT_CORE;
}
