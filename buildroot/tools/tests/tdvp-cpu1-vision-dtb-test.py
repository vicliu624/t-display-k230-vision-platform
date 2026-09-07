#!/usr/bin/env python3
"""Validate compiled ownership DTBs, resolving phandles and rejecting drift.

The before tree is the real fully patched CPU0 camera board, not a fixture.
This is a declaration test, not proof that either firmware obeys ownership.
"""
import copy
import struct
import sys
from pathlib import Path


def read_tree(path):
    blob = Path(path).read_bytes()
    magic, size, structure, strings = struct.unpack_from(">4I", blob)
    assert magic == 0xD00DFEED and size == len(blob)
    nodes, stack, cursor = {}, [], structure
    while True:
        token = struct.unpack_from(">I", blob, cursor)[0]
        cursor += 4
        if token == 1:
            end = blob.index(b"\0", cursor)
            stack.append(blob[cursor:end].decode())
            cursor = (end + 4) & ~3
            nodes["/".join(stack) or "/"] = {}
        elif token == 2:
            stack.pop()
        elif token == 3:
            length, name_offset = struct.unpack_from(">2I", blob, cursor)
            cursor += 8
            end = blob.index(b"\0", strings + name_offset)
            name = blob[strings + name_offset:end].decode()
            nodes["/".join(stack) or "/"][name] = blob[cursor:cursor + length]
            cursor = (cursor + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            assert not stack
            return nodes
        else:
            raise AssertionError(("invalid FDT token", token))


def cells(value):
    assert len(value) % 4 == 0
    return struct.unpack(">" + "I" * (len(value) // 4), value)


def normalized(nodes):
    handles = {cells(p["phandle"])[0]: path for path, p in nodes.items() if "phandle" in p}
    specifiers = {"clocks": "#clock-cells", "assigned-clocks": "#clock-cells",
                  "assigned-clock-parents": "#clock-cells", "pwms": "#pwm-cells",
                  "dmas": "#dma-cells", "resets": "#reset-cells",
                  "power-domains": "#power-domain-cells", "interrupts-extended": "#interrupt-cells"}
    plain = {"interrupt-parent", "remote-endpoint", "ports", "memory-region",
             "tdvp,gpio-controller", "tdvp,power-controller", "tdvp,clock-controller",
             "canaan,k230-audio-codec", "canaan,k230-i2s-controller"}
    result = {}
    for path, props in nodes.items():
        result[path] = {}
        for key, value in props.items():
            if key in ("phandle", "linux,phandle"):
                continue
            kind = "#gpio-cells" if key.endswith("-gpios") and key != "snps,nr-gpios" else specifiers.get(key)
            if kind or key in plain or (key.startswith("pinctrl-") and key[8:].isdigit()):
                data, entries = list(cells(value)), []
                while data:
                    handle = data.pop(0)
                    if not handle:
                        entries.append(None)
                        continue
                    target = handles[handle]
                    count = cells(nodes[target][kind])[0] if kind else 0
                    assert len(data) >= count
                    entries.append((target, tuple(data[:count])))
                    del data[:count]
                value = tuple(entries)
            result[path][key] = value
    return result


CLOCK = "/soc/sysctl/sysctl_clock@91100000/"
POWER = "/soc/sysctl/sysctl_power@91103000"
GPIO = "/soc/gpio@9140b000"
MMZ = "/reserved-memory/cpu1-mmz@14000000"
SHARED = "/reserved-memory/cpu1-transport@1c000000"
VISION = "/cpu1-vision"
DISABLED = ["/soc/i2c@91409000", "/soc/i2c@91409000/gc2093@37",
            "/soc/isp.0", "/soc/mipi.0", "/soc/mipi.1", "/soc/mipi.2",
            "/soc/gnne@80400000", "/soc/ai2d@80400c00"]
DISABLED += [CLOCK + name for name in ("i2c4_clk", "i2c4_pclk_gate", "tdvp_sensor_mclk1",
                                      "tdvp_sensor_mclk1_mux", "ai_clk", "ai_aclk")]
REMOVED = {
    "/soc/i2c@91409000": {"pinctrl-names", "pinctrl-0"},
    "/soc/mipi.2": {"reset-gpios", "pinctrl-names", "pinctrl-0", "clocks", "clock-names",
                    "assigned-clocks", "assigned-clock-parents", "assigned-clock-rates"},
}


def validate_shared_clock_layout(nodes):
    """Prove active real DT clock nodes fit 0069's guarded register layout."""
    count = 0
    for path, props in nodes.items():
        if not path.startswith(CLOCK) or props.get("status") == b"disabled\0":
            continue
        if props.get("compatible") != b"canaan,k230-clk-composite\0":
            continue
        values = {key: cells(value)[0] for key, value in props.items()
                  if key.startswith("clk-") and len(value) == 4}
        offsets = [values.get(key) for key in ("clk-gate-reg-offset", "clk-rate-reg-offset",
                                             "clk-rate-reg-offset_1", "clk-parent-reg-offset")]
        gate, rate, rate1, mux = offsets
        shared = {0x24, 0x2c, 0x30}
        if not shared.intersection(offsets):
            continue
        count += 1
        assert mux not in shared and rate1 not in shared, ("unsupported shared clock layout", path)
        assert rate not in shared or rate1 is None, ("shared dual-register divider", path)
        if gate in shared:
            assert gate == 0x24
            assert values["clk-gate-reg-bit-enable"] not in (10, 25)
            assert 0 <= values["clk-gate-reg-bit-enable"] < 32
            assert values["clk-gate-reg-bit-reverse"] == 0
        if rate in shared:
            assert rate != 0x24 and values["clk-rate-calc-method"] == 1
            assert values["clk-rate-reg-write-enable-bit"] == 31
            assert values["clk-rate-reg-mul-value-mask"] == 0
            shift = values["clk-rate-reg-div-value-shift"]
            mask = values["clk-rate-reg-div-value-mask"] << shift
            assert 0 <= shift < 32 and mask < 0x80000000
            assert rate != 0x2c or not mask & (7 << 27), ("Linux I2C4 divider ownership", path)
    assert count >= 20, "real LS clock provider layout was not examined"


def validate(before, after):
    assert set(after) - set(before) == {MMZ, SHARED, VISION}, "unexpected added nodes"
    assert set(before) <= set(after), "removed existing nodes"
    allowed = {path: {"status"} for path in DISABLED}
    for path, properties in REMOVED.items():
        allowed[path].update(properties)
    allowed[GPIO] = {"tdvp,cpu1-gpio-mask"}
    allowed[GPIO + "/gpio-port@0"] = {"gpio-reserved-ranges"}
    allowed[POWER] = {"tdvp,cpu1-vision-domains"}
    allowed[CLOCK.rstrip("/")] = {"tdvp,cpu1-i2c4-clock-sharing"}
    for path, properties in before.items():
        for key in properties.keys() | after[path].keys():
            if properties.get(key) != after[path].get(key):
                assert key in allowed.get(path, set()), ("unrelated DT drift", path, key)
    for path in DISABLED:
        assert after[path]["status"] == b"disabled\0", ("Linux still owns", path)
    for path, properties in REMOVED.items():
        assert not properties & after[path].keys(), ("retained CPU0 camera binding", path)
    assert cells(after[GPIO]["tdvp,cpu1-gpio-mask"]) == (0x200000,)
    assert cells(after[GPIO + "/gpio-port@0"]["gpio-reserved-ranges"]) == (21, 1)
    assert after[POWER]["tdvp,cpu1-vision-domains"] == b""
    assert after[CLOCK.rstrip("/")]["tdvp,cpu1-i2c4-clock-sharing"] == b""
    validate_shared_clock_layout(after)
    for path, base, length in [(MMZ, 0x14000000, 0x8000000), (SHARED, 0x1C000000, 0x2000000)]:
        assert cells(after[path]["reg"]) == (0, base, 0, length)
        assert after[path]["no-map"] == b"" and "reusable" not in after[path]
        assert set(after[path]) == {"reg", "no-map"}
    assert after[VISION] == {
        "compatible": b"tdvp,cpu1-vision-v1\0", "status": b"okay\0",
        "memory-region": ((SHARED, ()), (MMZ, ())), "memory-region-names": b"transport\0mmz\0",
        "tdvp,gpio-controller": ((GPIO, ()),), "tdvp,power-controller": ((POWER, ()),),
        "tdvp,clock-controller": ((CLOCK.rstrip("/"), ()),),
    }
    # The existing firmware allocation, mailbox and 512 MiB CMA must survive.
    assert cells(after["/reserved-memory/cpu1-runtime@10000000"]["reg"]) == (0, 0x10000000, 0, 0x4000000)
    assert cells(after["/cpu1-mailbox@13ff0000"]["reg"]) == (0, 0x13FF0000, 0, 0x10000)
    assert after["/soc/serial@91401000"]["status"] == b"okay\0", "missing UART1 baseline"
    reservations = []
    for path, properties in after.items():
        if path.startswith("/reserved-memory/") and "reg" in properties:
            values = cells(properties["reg"])
            assert len(values) == 4 and values[0] == values[2] == 0
            reservations.append((values[1], values[1] + values[3], path))
    reservations.sort()
    for previous, current in zip(reservations, reservations[1:]):
        assert previous[1] <= current[0], ("overlapping reservation", previous, current)


before, after = (normalized(read_tree(p)) for p in sys.argv[1:])
validate(before, after)
mutations = [(p, "status", b"okay\0") for p in DISABLED]
mutations += [(GPIO, "tdvp,cpu1-gpio-mask", struct.pack(">I", 0)),
              (POWER, "tdvp,cpu1-vision-domains", None), (MMZ, "no-map", None),
              (CLOCK.rstrip("/"), "tdvp,cpu1-i2c4-clock-sharing", None),
              (VISION, "tdvp,clock-controller", ((POWER, ()),)),
              (SHARED, "reg", struct.pack(">4I", 0, 0x14000000, 0, 0x2000000)),
              ("/cpu1-mailbox@13ff0000", "status", b"disabled\0"),
              ("/soc/serial@91401000", "status", b"disabled\0")]
for path, key, value in mutations:
    changed = copy.deepcopy(after)
    if value is None:
        changed[path].pop(key)
    else:
        changed[path][key] = value
    try:
        validate(before, changed)
    except (AssertionError, KeyError):
        continue
    raise AssertionError(("accepted mutation", path, key))
print(f"CPU1 vision DTB: PASS {len(before)} existing nodes checked; only ownership deltas; {len(mutations)} invalid candidates rejected")
