import os

# Must happen before any PySide6 import anywhere in the test session -- Qt picks its
# platform plugin at first use. "offscreen" lets GUI widgets be constructed and
# exercised in CI/headless environments with no real display.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
