"""Exercise the unmodified community calculator with simulated MicroHydra devices.

This demonstrates that its changing value can be extracted as semantic text; it
does not synthesize speech or emulate ESP32 timing, audio, memory, or Wi-Fi.
"""
import builtins
import runpy
import sys
import time
import types
from pathlib import Path

events = []
keys = iter([["2"], ["+"], ["3"], ["ENT"], ["c"], ["1"], ["/"], ["0"], ["ENT"]])

class Done(Exception):
    pass

class Display:
    def fill(self, *args): pass
    def hline(self, *args, **kwargs): pass
    def vline(self, *args, **kwargs): pass
    def show(self): pass
    def text(self, *args, **kwargs):
        if kwargs.get("x") == 10 and kwargs.get("y") == 50:
            events.append(kwargs["text"])

class Input:
    def get_new_keys(self):
        try: return next(keys)
        except StopIteration: raise Done

lib = types.ModuleType("lib")
lib.display = types.SimpleNamespace(Display=Display)
lib.userinput = types.SimpleNamespace(UserInput=Input)
hydra = types.ModuleType("lib.hydra")
hydra.config = types.SimpleNamespace(Config=lambda: types.SimpleNamespace(palette=list(range(16))))
font = types.ModuleType("font")
font.vga1_8x16 = font.vga2_16x32 = object()
sys.modules.update({"lib": lib, "lib.hydra": hydra, "font": font})
builtins.const = lambda value: value
time.sleep_ms = lambda ms: None
try:
    runpy.run_path(str(Path(__file__).parent / "MicroHydra-Apps/app-source/Calculator/Calculator.py"))
except Done:
    pass
expected = ["2", "2+", "2+3", "5", "0", "1", "1/", "1/0", "ERROR"]
assert events == expected, (events, expected)
print("PASS: captured calculator values:", ", ".join(events))
