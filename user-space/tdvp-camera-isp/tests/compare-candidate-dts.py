#!/usr/bin/env python3
"""Compare dtc -s decompilations, resolving phandles rather than their numbers."""
import re
import sys
from pathlib import Path

def parse(path):
    nodes, stack = {}, []
    for raw in Path(path).read_text().splitlines():
        line = raw.strip()
        if not line or line == '/dts-v1/;':
            continue
        if line.endswith(' {'):
            name = line[:-2]
            stack.append('' if name == '/' else name)
            nodes['/'.join(stack) or '/'] = {}
        elif line == '};':
            stack.pop()
        else:
            assert line.endswith(';') and stack, line
            key, separator, value = line[:-1].partition(' = ')
            nodes['/'.join(stack) or '/'][key] = value if separator else ''
    return nodes

def cells(value):
    assert value.startswith('<') and value.endswith('>'), value
    return [int(v, 0) for v in value[1:-1].split()]

def normalize(nodes):
    handles = {cells(props['phandle'])[0]: path for path, props in nodes.items()
               if 'phandle' in props}
    cell_types = {'clocks': '#clock-cells', 'assigned-clocks': '#clock-cells',
                  'assigned-clock-parents': '#clock-cells', 'pwms': '#pwm-cells',
                  'dmas': '#dma-cells', 'resets': '#reset-cells',
                  'power-domains': '#power-domain-cells',
                  'interrupts-extended': '#interrupt-cells'}
    plain = {'interrupt-parent', 'remote-endpoint', 'ports',
             'canaan,k230-audio-codec', 'canaan,k230-i2s-controller'}
    result = {}
    for path, props in nodes.items():
        result[path] = {}
        for key, value in props.items():
            if key in ('phandle', 'linux,phandle'):
                continue
            cell_type = '#gpio-cells' if key.endswith('-gpios') and key != 'snps,nr-gpios' else cell_types.get(key)
            if cell_type or key in plain or re.fullmatch(r'pinctrl-\d+', key):
                data, entries = cells(value), []
                while data:
                    handle = data.pop(0)
                    if handle == 0:
                        entries.append(None)
                        continue
                    target = handles[handle]
                    assert not cell_type or cell_type in nodes[target], (path, key, target, cell_type, value)
                    count = cells(nodes[target][cell_type])[0] if cell_type else 0
                    assert len(data) >= count
                    entries.append((target, tuple(data[:count])))
                    data = data[count:]
                value = tuple(entries)
            result[path][key] = value
    return result

before, after = map(lambda p: normalize(parse(p)), sys.argv[1:])
new_nodes = {
    '/soc/i2c@91409000/gc2093@37',
    '/soc/iomux@91105000/tdvp-camera-i2c4-pins',
    '/soc/iomux@91105000/tdvp-camera-mclk1-pin',
    '/soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1',
    '/soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1_mux',
}
allowed = {
    '/aliases': {'i2c0', 'i2c1', 'i2c4'},
    '/soc/i2c@91409000': {'status', 'pinctrl-names', 'pinctrl-0'},
    '/soc/mipi.0': {'status'},
    '/soc/mipi.2': {'status', 'reset-gpios', 'pinctrl-0', 'pinctrl-names',
                    'clocks', 'clock-names', 'assigned-clocks',
                    'assigned-clock-parents', 'assigned-clock-rates'},
}
assert set(before) <= set(after), 'existing nodes removed'
assert set(after) - set(before) == new_nodes, 'unexpected added nodes'
for path, props in before.items():
    for key in props.keys() | after[path].keys():
        if props.get(key) != after[path].get(key):
            assert key in allowed.get(path, set()), (path, key, props.get(key), after[path].get(key))
            print('camera delta:', path, key, repr(after[path].get(key)))
assert after['/aliases']['i2c0'] == '"/soc/i2c@91408000"'
assert after['/aliases']['i2c1'] == '"/i2c-gpio"'
assert after['/aliases']['i2c4'] == '"/soc/i2c@91409000"'
assert after['/soc/mipi.0']['status'] == '"disabled"'
assert after['/soc/mipi.1']['status'] == '"disabled"'
assert after['/soc/mipi.2']['status'] == '"okay"'
assert cells(after['/soc/mipi.2']['reg']) == [0, 0x9000a800, 0, 0x800]
assert cells(after['/soc/mipi.2']['interrupts']) == [121, 4]
print(f'PASS: {len(before)} existing DT nodes preserved outside explicit camera properties; phandles resolved')
