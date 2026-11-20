"""Start the packaged builder, applying the installed application overlay."""

import json
import os
from pathlib import Path
import re
import runpy
import sys


if __name__ == "__main__":
    package = Path(__file__).resolve().parents[1]
    os.environ["CARDPUTER_BUILDER_ROOT"] = str(package)
    os.environ["PYTHONNOUSERSITE"] = "1"
    application = package / "builder"
    try:
        pointer = package / "workspace/program-update.json"
        release = json.loads(pointer.read_text(encoding="utf-8"))["version"]
        if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", release):
            candidate = package / "workspace/program" / release
            if (candidate / "app.py").is_file():
                application = candidate
    except (OSError, ValueError, KeyError, TypeError):
        pass
    sys.path.insert(0, str(application))
    runpy.run_path(str(application / "app.py"), run_name="__main__")
