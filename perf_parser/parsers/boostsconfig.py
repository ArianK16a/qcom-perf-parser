import sys
import re
import xml.etree.ElementTree as ET
from typing import List, Optional, Tuple

from perf_parser.models import Boost


def parse_resources(resources_str: str) -> List[Tuple[int, int]]:
    """
    Parse a string like
    "0x40C00000, 0x1, 0x40804000, 0xFFF, ..."
    into a list of (int, int) tuples.
    """
    # Accept comma/space/newline separated values; extract all ints/hex tokens robustly.
    toks = re.findall(r'0x[0-9a-fA-F]+|\d+', resources_str or "")
    nums = [int(tok, 0) for tok in toks]
    return [(nums[i], nums[i + 1]) for i in range(0, len(nums), 2)]


def parse_targets(target_str: str) -> List[str]:
    if not target_str:
        return []
    targets = target_str.split(',')
    return [t.strip() for t in targets]


def parse_fps(fps_str: Optional[str]) -> List[int]:
    if not fps_str:
        return []
    fps_values = fps_str.split(',')
    return [int(fps, 0) for fps in fps_values]


def parse_boost_xml(filename: str) -> List[Boost]:
    tree = ET.parse(filename)
    root = tree.getroot()

    perfboosts: List[Boost] = []

    for cfg in root.iter('Config'):
        perfboosts.append(
            Boost(
                id=int(cfg.attrib['Id'], 0),
                type=int(cfg.attrib.get('Type', '-1'), 0),
                enable=cfg.attrib['Enable'].lower() == 'true',
                timeout=int(cfg.attrib.get('Timeout', '0'), 0),
                target=parse_targets(cfg.attrib.get('Target', '')),
                resources=parse_resources(cfg.attrib['Resources']),
                fps=parse_fps(cfg.attrib.get('Fps')),
            )
        )

    return perfboosts


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('usage: python perfboostsconfig.py perfboostsconfig.xml')
        sys.exit()
    perfboost_list = parse_boost_xml(sys.argv[1])

    for pb in perfboost_list:
        print(pb)
