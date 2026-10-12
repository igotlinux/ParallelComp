import sys
import os

# Automatically ensure python/ is in sys.path for all unit tests
repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
python_dir = os.path.join(repo_root, "python")
if python_dir not in sys.path:
    sys.path.insert(0, python_dir)
