#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// perf boost config tags
#define BOOSTS_CONFIGS_XML_ROOT "BoostConfigs"
#define BOOSTS_CONFIGS_XML_CHILD_CONFIG "PerfBoost"
#define BOOSTS_CONFIGS_XML_ELEM_CONFIG_TAG "Config"
#define BOOSTS_CONFIGS_XML_ELEM_RESOURCES_TAG "Resources"
#define BOOSTS_CONFIGS_XML_ELEM_ENABLE_TAG "Enable"
#define BOOSTS_CONFIGS_XML_ELEM_ID_TAG "Id"
#define BOOSTS_CONFIGS_XML_ELEM_TYPE_TAG "Type"
#define BOOSTS_CONFIGS_XML_ELEM_TIMEOUT_TAG "Timeout"
#define BOOSTS_CONFIGS_XML_ELEM_KERNEL_TAG "Kernel"
#define BOOSTS_CONFIGS_XML_ELEM_FPS_TAG "Fps"
#define BOOSTS_CONFIGS_XML_ELEM_TARGET_TAG "Target"
#define BOOSTS_CONFIGS_XML_DIVERGENT_TAG "Divergent"

#define MAX_RESOURCES_PER_REQUEST 64
#define MAX_ARGS_PER_REQUEST (MAX_RESOURCES_PER_REQUEST * 2)
#define MAX_OPCODE_VALUE_TABLE_SIZE MAX_ARGS_PER_REQUEST

// https://git.codelinaro.org/clo/la/platform/frameworks/base/-/blob/LA.QSSI.16.0.r4-02700-qssi.0/core/java/android/util/BoostFramework.java
// sed "s/public static final int \([A-Z0-9_]\+\)[ ]\+=[ ]\+\([xX0-9A-Fa-f]\+\);/{\2, \"\1\"},/g"
std::unordered_map<int, std::string> hintMap = {
        // perf hints
        {0x00001080, "VENDOR_HINT_SCROLL_BOOST"},
        {0x00001081, "VENDOR_HINT_FIRST_LAUNCH_BOOST"},
        {0x00001082, "VENDOR_HINT_SUBSEQ_LAUNCH_BOOST"},
        {0x00001083, "VENDOR_HINT_ANIM_BOOST"},
        {0x00001084, "VENDOR_HINT_ACTIVITY_BOOST"},
        {0x00001085, "VENDOR_HINT_TOUCH_BOOST"},
        {0x00001086, "VENDOR_HINT_MTP_BOOST"},
        {0x00001087, "VENDOR_HINT_DRAG_BOOST"},
        {0x00001088, "VENDOR_HINT_PACKAGE_INSTALL_BOOST"},
        {0x00001089, "VENDOR_HINT_ROTATION_LATENCY_BOOST"},
        {0x00001090, "VENDOR_HINT_ROTATION_ANIM_BOOST"},
        {0x00001091, "VENDOR_HINT_PERFORMANCE_MODE"},
        {0x00001092, "VENDOR_HINT_APP_UPDATE"},
        {0x00001093, "VENDOR_HINT_KILL"},
        {0x00001096, "VENDOR_HINT_BOOST_RENDERTHREAD"},
        {0x0000109C, "VENDOR_HINT_PASS_PID"},
        {0x000010AA, "VENDOR_HINT_SCENARIO_GPU"},
        {0x000010AB, "VENDOR_HINT_SCENARIO_CPU"},
        {0x000010AC, "VENDOR_HINT_SCENARIO_CPU_GPU"},
        {0x000010AD, "VENDOR_HINT_SCENARIO_CPU_AGGRESSIVE"},
        // perf events
        {0x00001042, "VENDOR_HINT_FIRST_DRAW"},
        {0x00001043, "VENDOR_HINT_TAP_EVENT"},
        {0x00001051, "VENDOR_HINT_DRAG_START"},
        {0x00001052, "VENDOR_HINT_DRAG_END"},
        // Ime Launch Boost Hint
        {0x0000109F, "VENDOR_HINT_IME_LAUNCH_EVENT"},
        // App exit animation boost
        {0x000010A9, "VENDOR_HINT_EXIT_ANIM_BOOST"},

        // feedback hints
        {0x00001601, "VENDOR_FEEDBACK_WORKLOAD_TYPE"},
        {0x00001602, "VENDOR_FEEDBACK_LAUNCH_END_POINT"},
        {0x00001604, "VENDOR_FEEDBACK_PA_FW"},

        // UXE Events and Triggers
        {1, "UXE_TRIGGER"},
        {2, "UXE_EVENT_BINDAPP"},
        {3, "UXE_EVENT_DISPLAYED_ACT"},
        {4, "UXE_EVENT_KILL"},
        {5, "UXE_EVENT_GAME"},
        {6, "UXE_EVENT_SUB_LAUNCH"},
        {7, "UXE_EVENT_PKG_UNINSTALL"},
        {8, "UXE_EVENT_PKG_INSTALL"},

        // New Hints while porting IOP to Perf Hal.
        {0x000010A0, "VENDOR_HINT_BINDAPP"},
        {0x000010A1, "VENDOR_HINT_WARM_LAUNCH"},  // SUB_LAUNCH
        // 0x000010A2 is added in UXPerformance.java for SPEED Hints
        {0x000010A3, "VENDOR_HINT_PKG_INSTALL"},
        {0x000010A4, "VENDOR_HINT_PKG_UNINSTALL"},

        // perf opcodes
        {0X42820000, "MPCTLV3_GPU_IS_APP_FG"},
        {0X42824000, "MPCTLV3_GPU_IS_APP_BG"},
};

#define EXTRACT_VERSION 0xC0000000
#define EXTRACT_OPCODE_TYPE 0x20000000
#define EXTRACT_MAJOR_TYPE 0x1FC00000
#define EXTRACT_MINOR_TYPE 0x000FC000
#define EXTRACT_MAP_TYPE 0x00002000
#define EXTRACT_CLUSTER 0x00000F00
#define EXTRACT_CORE 0x00000070
#define EXTRACT_MPAM_INDEX 0x0000003F

#define SHIFT_BIT_VERSION 30
#define SHIFT_BIT_OPCODE_TYPE 29
#define SHIFT_BIT_MAJOR_TYPE 22
#define SHIFT_BIT_MINOR_TYPE 14
#define SHIFT_BIT_CLUSTER 8
#define SHIFT_BIT_CORE 4
#define SHIFT_BIT_MAP 13

/*All resources are categorized into one of the following types
based their sysfs node manipulations.*/
enum ResourceType {
    /*Resources which updates a single normal node can be categorized as SINGLE_NODE.*/
    SINGLE_NODE,
    /* All the resources whose sysfs node paths can go offline depending on the core
    status(offline/online) and also updating a single core automatically updates all the
    cores of that cluster, can be assigned as type INTERACTIVE_NODE.
    Example -  schedutil_hispeed_freq, we find the first online core in the requested cluster
    and update it with requested vlaue. As a result all the cores in that cluster automatically
    gets updated with the same value for schedutil_hispeed_freq.*/
    INTERACTIVE_NODE,
    /*The resources for which we manually update all the cores of both clusters with a same
    value basing on the requested value, can be categorized as UPDATE_ALL_CORES.
    Example - sched_prefer_idle, when we get a acquire request call for this resource, we
    update the given Sysfs node for all the cores of both clusters with a same value.*/
    UPDATE_ALL_CORES,
    /*A special type of UPDATE_ALL_CORES, where we manually update cores of both clusters with
    different values basing on the requested value, can be assigned as UPDATE_CORES_PER_CLUSTER.
    Example - sched_mostly_idle_freq, For an acquire request call we update the given Sysfs node for
    all the cores but with different values for different clusters.*/
    UPDATE_CORES_PER_CLUSTER,
    /*All the resources which provide an option to select a particular core to get updated
    can be assigned as type SELECT_CORE_TO_UPDATE
    Example - sched_static_cpu_pwr_cost, for this resource in the Opcode we can provide the
    exact core number which needs to be updated.*/
    SELECT_CORE_TO_UPDATE,
    /*All other resources which update multiple nodes or needs special treament.*/
    SPECIAL_NODE,
    /*Adding memlat as a node type for handling memlat resource request*/
    MEM_LAT_NODE,
};

class Resource {
  public:
    Resource(int opcode, int value)
        : opcode(opcode),
          value(value),
          version((opcode & EXTRACT_VERSION) >> SHIFT_BIT_VERSION),
          mapping((opcode & EXTRACT_MAP_TYPE) >> SHIFT_BIT_MAP),
          major((opcode & EXTRACT_MAJOR_TYPE) >> SHIFT_BIT_MAJOR_TYPE),
          minor((opcode & EXTRACT_MINOR_TYPE) >> SHIFT_BIT_MINOR_TYPE),
          cluster((opcode & EXTRACT_CLUSTER) >> SHIFT_BIT_CLUSTER),
          core((opcode & EXTRACT_CORE) >> SHIFT_BIT_CORE),
          mpamIndex((opcode & EXTRACT_MPAM_INDEX)) {}

    int opcode;
    int value;

    // ResourceType type;
    int version;
    int mapping;
    int major;
    int minor;
    int cluster;
    int core;
    int mpamIndex;
};

struct PerfBoost {
    std::vector<Resource> resources;
    bool enable;
    int id;
    int type;
    int timeout;
    std::string kernel;
    std::string target;
    // TODO eigentlich array
    int fps;
};

struct ResourceConfig {
    int major;
    int minor;
    bool supported;
    // TODO disp mode
    std::string node;
};

struct HintKey {
    int id;
    int type;
    int fps;

    bool operator==(const HintKey& other) const noexcept {
        return id == other.id && type == other.type && fps == other.fps;
    }
};

struct HintKeyHash {
    std::size_t operator()(const HintKey& k) const noexcept {
        std::size_t h1 = std::hash<int>()(k.id);
        std::size_t h2 = std::hash<int>()(k.type);
        std::size_t h3 = std::hash<int>()(k.fps);
        return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
    }
};

struct NodeInfo {
    std::string name;
    std::string path;
    std::set<std::string> values;
    int defaultIndex = 0;
    bool resetOnInit = true;
};
