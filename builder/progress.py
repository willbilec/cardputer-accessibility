"""Turn PlatformIO's pipe output into log lines and live progress updates."""

import codecs
import locale
import re

PROGRESS = re.compile(r"^\s*(Downloading|Unpacking)\s+(?:\d{1,3}%\s*)+$")
PERCENT = re.compile(r"(?<!\d)(\d{1,3})%")


def output_events(stream):
    decode = codecs.getincrementaldecoder(locale.getpreferredencoding(False))(errors="replace")
    pending = ""
    previous = None
    while True:
        raw = stream.read1(8192)
        pending += decode.decode(raw, final=not raw)
        lines = pending.replace("\r\n", "\n").replace("\r", "\n").split("\n")
        pending = lines.pop()
        if not raw and pending:
            lines.append(pending)
            pending = ""
        for line in lines:
            match = PROGRESS.fullmatch(line)
            if match:
                percent = PERCENT.findall(line)[-1]
                update = f"{match[1]}: {percent}%"
                if update != previous:
                    yield "progress", update
                previous = update
                if percent == "100":
                    yield "log", update
                    previous = None
            elif line:
                previous = None
                yield "log", line
        match = re.match(r"^\s*(Downloading|Unpacking)\s+", pending)
        if match:
            percentages = PERCENT.findall(pending)
            if percentages:
                update = f"{match[1]}: {percentages[-1]}%"
                if update != previous:
                    yield "progress", update
                    previous = update
        if not raw:
            break
