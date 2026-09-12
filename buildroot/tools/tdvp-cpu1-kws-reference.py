#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate pinned nncase 2.9 host references, never board/ASR acceptance.

usage: tdvp-cpu1-kws-reference.py <official-kws.kmodel> <new-output-directory>
Requires nncase==2.9.0, nncase-kpu==2.9.0, numpy==1.26.4. The native simulator
and its executable must be discoverable through the pinned wheel's library/
executable search paths. This script neither downloads nor compiles a model.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import struct

import numpy as np
# nncase/__init__.py re-exports exactly these native bindings but also starts
# its .NET compiler. We need only simulation of the already compiled model.
from _nncase import RuntimeTensor, Simulator

if not __debug__:
    raise SystemExit('Reference assertions must not be disabled with Python -O')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('model', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
assert importlib.metadata.version('nncase') == '2.9.0'
assert importlib.metadata.version('nncase-kpu') == '2.9.0'
assert np.__version__ == '1.26.4'
model = args.model.read_bytes()
model_sha = 'b51a31c3310a052488cbce9fbbc52a1d9957f574bc31f1969a1757c7917ae8b4'
assert hashlib.sha256(model).hexdigest() == model_sha, 'Not the pinned official KWS model'
assert len(model) == 369560 and struct.unpack_from('<2I', model) == (0x4b4d444c, 7)
out = args.output.resolve()
out.mkdir(exist_ok=False)  # Never overwrite an earlier run's evidence.
os.chdir(out)  # The vendor simulator writes its gmodel dump under the cwd.
sim = Simulator()
sim.load_model(model)
assert sim.inputs_size == 2 and sim.outputs_size == 2
input_shapes = [[1, 30, 40], [1, 256, 105]]
output_shapes = [[1, 30, 2], [1, 256, 105]]
for i in range(2):
    assert sim.get_input_shape(i) == input_shapes[i]
    assert sim.get_output_shape(i) == output_shapes[i]
    assert sim.get_input_desc(i).dtype == np.dtype('float32')
    assert sim.get_output_desc(i).dtype == np.dtype('float32')
expected_hashes = [
    ['d5f764195859cfc491030426dbb8c489d8c4e3745d762afceec596e2c30e84fc',
     '0c7888f4f3d8e5ace74081520fc164afcfa25fa34ebcb4c17e22f246176576e1'],
    ['3dae950d114a6e019f23eb3eace33e3bca9399019f78fad6de0226636fb53b7a',
     '27c5e3eee94a2aa44e5a504a4c4e9d13b91253235d2f893c365584573c52ec32'],
    ['2e1e5d9c11ada6b95d5ea89a18ab2c2dfa39a47543d6f884e8885b6ad8df5e65',
     'af6cdb1068a8eb3209d343d6caa901a93944e1b59807223735ab9a87c5539d59'],
    ['a3ea39c33bcc5b640563a74932a83457d9132a6e4d7534b05c1649608949744e',
     '5e9566bafb5b551e55ec52f361bf74b8693db7248c9630129c6421f3d6923239'],
]
manifest = {'model_sha256': model_sha, 'nncase': '2.9.0', 'nncase_kpu': '2.9.0',
            'numpy': np.__version__, 'input_shapes': input_shapes, 'output_shapes': output_shapes,
            'absolute_tolerance': 1e-5, 'relative_tolerance': 1e-4,
            'acceptance_scope': 'host reference only; no hardware or speech recognition acceptance',
            'cases': []}
previous = np.zeros(input_shapes[1], dtype=np.float32)
for case in range(4):
    signal = np.zeros(input_shapes[0], dtype=np.float32)
    if case == 1:
        signal.flat[:] = ((np.arange(signal.size) % 31 - 15) / 16).astype(np.float32)
    elif case == 2:
        signal.flat[::37] = np.float32(4)
        signal.flat[::53] = np.float32(-2)
    elif case == 3:
        signal.flat[:] = ((np.arange(signal.size) % 17 - 8) / 8).astype(np.float32)
    cache = previous.copy() if case >= 2 else np.zeros(input_shapes[1], dtype=np.float32)
    record = {'id': case, 'files': []}
    for i, array in enumerate([signal, cache]):
        sim.set_input_tensor(i, RuntimeTensor.from_numpy(array))
        name = f'case{case}.input{i}.f32'
        array.astype('<f4').tofile(out / name)
        record['files'].append({'name': name, 'bytes': array.nbytes,
                               'sha256': hashlib.sha256((out / name).read_bytes()).hexdigest()})
    sim.run()
    for i in range(2):
        result = sim.get_output_tensor(i).to_numpy().copy()
        assert list(result.shape) == output_shapes[i]
        assert result.dtype == np.float32 and np.isfinite(result).all()
        name = f'case{case}.output{i}.f32'
        encoded = result.astype('<f4').tobytes()
        digest = hashlib.sha256(encoded).hexdigest()
        assert digest == expected_hashes[case][i], f'Host reference drift in case {case}, output {i}'
        (out / name).write_bytes(encoded)
        record['files'].append({'name': name, 'bytes': len(encoded), 'sha256': digest,
                               'min': float(result.min()), 'max': float(result.max())})
        if i == 1:
            previous = result
    print(json.dumps(record), flush=True)
    manifest['cases'].append(record)
# Only publish the final manifest after all exact reference hashes pass.
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print('PASS four pinned official KWS host reference cases; board KPU NOT executed')
