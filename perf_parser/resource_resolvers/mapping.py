from typing import Dict

from perf_parser.models import ResourceKey, ResourceResolver
from perf_parser.resource_resolvers import (
    cluster,
    core_ctl,
    gpu,
    hyst,
    msm_perf,
    sched_migrate,
    walt,
)

resource_resolvers: Dict[ResourceKey, ResourceResolver] = {
    (0x2, 0x0): msm_perf.resolve_msm_perf,  # msm_perf - min freq
    (0x2, 0x1): msm_perf.resolve_msm_perf,  # msm_perf - max freq
    (0x2, 0x2): walt.resolve_walt_path,
    (0x2, 0x3): walt.resolve_walt_path,
    (0x2, 0x5): walt.resolve_walt_path, # walt - adaptive_level_1
    (0x3, 0x1A): walt.resolve_walt_path, # walt - boost
    (0x3, 0x20): lambda x: [],  # unsupported - sched_per_task_boost
    (0x3, 0x38): sched_migrate.resolve_sched_migrate,  # sched - up/down migrate
    (0x3, 0x3D): sched_migrate.resolve_sched_group_migrate,  # sched - group up/down migrate
    (0x4, 0x0): core_ctl.resolve_lock_min_cores,  # core_ctl - lock_min_cores
    (0x4, 0x2): cluster.resolve_cpu_cluster,  # core_ctl - enable
    **{(0x5, sub): cluster.resolve_cpu_cluster for sub in range(0x0, 0x17)},  # cpu governor tuning
    (0x6, 0x3): hyst.resolve_hyst_opt_path,  # CPUBW_HWMON_HYST_OPT_OPCODE
    (0x6, 0xE): hyst.resolve_hyst_opt_path,  # CPU_LLCC_BW_HYST_OPT
    (0x6, 0x17): hyst.resolve_hyst_opt_path,  # GOLD_CPU_LLCC_BW_HYST_OPT
    (0xA, 0x3): gpu.resolve_next_gpu_freq,  # gpu - min freq
    (0xA, 0x4): gpu.resolve_next_gpu_freq,  # gpu - max freq
    (0xC, 0x2): hyst.resolve_hyst_opt_path,  # LLCBW_HWMON_HYST_OPT_OPCODE
    (0xC, 0x9): hyst.resolve_hyst_opt_path,  # LLCC_DDR_BW_HYST_OPT
}
