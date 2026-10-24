# SPDX-License-Identifier: Apache-2.0
# scripts/pio_env_stamp.py — Ensures .pio/app object cache is never shared across different PIOENVs
Import("env")
import os
import shutil

pio_app_dir = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "app")
stamp_file = os.path.join(pio_app_dir, ".pioenv_stamp")
current_env = env.subst("$PIOENV")

prev_env = None
if os.path.isfile(stamp_file):
    with open(stamp_file, "r", encoding="utf-8") as f:
        prev_env = f.read().strip()

if prev_env != current_env and os.path.isdir(pio_app_dir):
    shutil.rmtree(pio_app_dir)

os.makedirs(pio_app_dir, exist_ok=True)
with open(stamp_file, "w", encoding="utf-8") as f:
    f.write(current_env + "\n")
