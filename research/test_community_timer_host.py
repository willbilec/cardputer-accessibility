"""Exercise the community timer's unmodified countdown function with fakes.

This is a logic/display test, not a device or speech test.
"""
import ast
from pathlib import Path
from types import SimpleNamespace

path = Path(__file__).parent / "MicroHydra-Apps/app-source/timer/timer.py"
tree = ast.parse(path.read_text(encoding="utf-8"))
countdown = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "start_timer")
program = ast.fix_missing_locations(ast.Module(body=[countdown], type_ignores=[]))

class Clock:
    def __init__(self): self.now = 0.0
    def time(self):
        self.now += 0.1
        return self.now

class Display:
    def __init__(self): self.labels = []
    def fill(self, *_): pass
    def text(self, label, *_, **__): self.labels.append(label)
    def show(self): pass

class AlarmTimer:
    PERIODIC = 1
    started = False
    stopped = False
    def __init__(self, _id, mode, period, callback):
        assert mode == self.PERIODIC and period == 1000
        AlarmTimer.started = True
        callback(self)
    def deinit(self): AlarmTimer.stopped = True

class Keyboard:
    def get_pressed_keys(self): return ["A"] if AlarmTimer.started else []

display = Display()
alerts = []
env = {
    "time": Clock(), "random": SimpleNamespace(randint=lambda a, b: a),
    "blight": SimpleNamespace(duty_u16=lambda _: None), "kb": Keyboard(),
    "tft": display, "bg_color": 0, "ui_color": 1, "big_font": None,
    "small_font": None, "Timer": AlarmTimer, "alarm": lambda: alerts.append("alarm"),
}
exec(compile(program, str(path), "exec"), env)
env["start_timer"]("2", "seconds")
assert any(label.startswith("00:0") for label in display.labels), display.labels
assert "00:00" in display.labels and "< any key >" in display.labels
assert alerts == ["alarm"] and AlarmTimer.stopped
print("PASS: original timer countdown reached 00:00, fired alarm, and accepted dismissal")
